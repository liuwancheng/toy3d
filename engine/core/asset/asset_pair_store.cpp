#include "asset_pair_store.h"
#include "asset_descriptor_path.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace toy3d
{
    namespace
    {
        constexpr std::uint8_t k_journal_magic[8] = {'T', 'O', 'Y', '3', 'D', 'T', 'X', 'N'};
        constexpr std::uint32_t k_journal_version = 2u;
        constexpr std::size_t k_max_depth = 64u;

        struct PairPaths
        {
            VirtualPath asset;
            VirtualPath meta;
            VirtualPath journal;
            VirtualPath move_journal;
            VirtualPath asset_stage;
            VirtualPath meta_stage;
            VirtualPath asset_backup;
            VirtualPath meta_backup;
        };

        struct Journal
        {
            AssetId id;
            bool had_asset = false;
            bool had_meta = false;
            bool has_meta = false;
            bool deleting = false;
            Sha256Hash old_asset_hash{};
            Sha256Hash old_meta_hash{};
            Sha256Hash new_asset_hash{};
            Sha256Hash new_meta_hash{};
        };

        AssetStatus fail(AssetErrorCode code, const VirtualPath& path, const char* message, const FileStatus& file = {})
        {
            return {code, {}, path.utf8(), {}, {}, message, file};
        }

        AssetResult<PairPaths> paths_for(const VirtualPath& path)
        {
            const std::string& name = path.utf8();
            VirtualPath meta_path;
            if (!asset_meta_path(path, meta_path))
            {
                return AssetResult<PairPaths>(
                    fail(AssetErrorCode::InvalidFormat, path, "descriptor path must end in .asset or .scene"));
            }
            PairPaths paths;
            paths.asset = path;
            const auto journal = VirtualPath::parse(name + ".txn");
            const auto move_journal = VirtualPath::parse(name + ".move");
            const auto asset_stage = VirtualPath::parse(name + ".new");
            const auto meta_stage = VirtualPath::parse(meta_path.utf8() + ".new");
            const auto asset_backup = VirtualPath::parse(name + ".old");
            const auto meta_backup = VirtualPath::parse(meta_path.utf8() + ".old");
            if (!journal.succeeded() || !move_journal.succeeded() || !asset_stage.succeeded() ||
                !meta_stage.succeeded() || !asset_backup.succeeded() || !meta_backup.succeeded())
            {
                return AssetResult<PairPaths>(
                    fail(AssetErrorCode::InvalidFormat, path, "invalid asset transaction path"));
            }
            paths.meta = meta_path;
            paths.journal = journal.value();
            paths.move_journal = move_journal.value();
            paths.asset_stage = asset_stage.value();
            paths.meta_stage = meta_stage.value();
            paths.asset_backup = asset_backup.value();
            paths.meta_backup = meta_backup.value();
            return AssetResult<PairPaths>(std::move(paths));
        }

        AssetResult<bool> exists(const FileSystem& files, const VirtualPath& path)
        {
            const auto found = files.stat(path);
            if (found.succeeded())
            {
                return AssetResult<bool>(true);
            }
            if (found.status().code == FileErrorCode::NotFound)
            {
                return AssetResult<bool>(false);
            }
            return AssetResult<bool>(fail(AssetErrorCode::Io, path, "asset stat failed", found.status()));
        }

        AssetResult<Sha256Hash> file_hash(const FileSystem& files, const VirtualPath& path)
        {
            const auto bytes = files.read_binary(path, AssetFileLimits{}.max_file_bytes);
            if (!bytes.succeeded())
            {
                return AssetResult<Sha256Hash>(
                    fail(AssetErrorCode::Io, path, "asset transaction read failed", bytes.status()));
            }
            return AssetResult<Sha256Hash>(sha256(bytes.value()));
        }

        AssetStatus verify_hash(const FileSystem& files, const VirtualPath& path, const Sha256Hash& expected)
        {
            const auto actual = file_hash(files, path);
            if (!actual.succeeded())
            {
                return actual.status();
            }
            if (actual.value() != expected)
            {
                return fail(AssetErrorCode::Conflict, path, "asset transaction file was changed by another writer");
            }
            return AssetStatus::success();
        }

        AssetStatus remove_owned(FileSystem& files, const VirtualPath& path, const Sha256Hash& expected)
        {
            const auto found = exists(files, path);
            if (!found.succeeded())
            {
                return found.status();
            }
            if (!found.value())
            {
                return AssetStatus::success();
            }
            const AssetStatus checked = verify_hash(files, path, expected);
            if (!checked.succeeded())
            {
                return checked;
            }
            const FileStatus removed = files.remove_file(path);
            return removed.succeeded() ? AssetStatus::success()
                                       : fail(AssetErrorCode::Io, path, "asset transaction cleanup failed", removed);
        }

        AssetStatus rename_file(FileSystem& files, const VirtualPath& source, const VirtualPath& destination)
        {
            const FileStatus renamed = files.rename_no_replace(source, destination);
            return renamed.succeeded() ? AssetStatus::success()
                                       : fail(AssetErrorCode::Io, source, "asset transaction rename failed", renamed);
        }

        ValueStatus write_hash(ValueWriter& writer, const Sha256Hash& hash)
        {
            for (std::uint8_t byte : hash)
            {
                const ValueStatus status = writer.write_uint8(byte);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            return ValueStatus::success();
        }

        ValueStatus read_hash(ValueReader& reader, Sha256Hash& hash)
        {
            for (std::uint8_t& byte : hash)
            {
                const ValueStatus status = reader.read_uint8(byte);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            return ValueStatus::success();
        }

        AssetResult<std::vector<std::uint8_t>> encode_journal(const Journal& journal)
        {
            ValueWriter writer;
            for (std::uint8_t byte : journal.id.bytes)
            {
                if (!writer.write_uint8(byte).succeeded())
                {
                    return AssetResult<std::vector<std::uint8_t>>(
                        fail(AssetErrorCode::InvalidFormat, {}, "journal ID encoding failed"));
                }
            }
            if (!writer.write_bool(journal.had_asset).succeeded() || !writer.write_bool(journal.had_meta).succeeded() ||
                !writer.write_bool(journal.has_meta).succeeded() || !writer.write_bool(journal.deleting).succeeded() ||
                !write_hash(writer, journal.old_asset_hash).succeeded() ||
                !write_hash(writer, journal.old_meta_hash).succeeded() ||
                !write_hash(writer, journal.new_asset_hash).succeeded() ||
                !write_hash(writer, journal.new_meta_hash).succeeded())
            {
                return AssetResult<std::vector<std::uint8_t>>(
                    fail(AssetErrorCode::InvalidFormat, {}, "journal encoding failed"));
            }
            std::vector<std::uint8_t> bytes(std::begin(k_journal_magic), std::end(k_journal_magic));
            ValueWriter header;
            if (!header.write_uint32(k_journal_version).succeeded())
            {
                return AssetResult<std::vector<std::uint8_t>>(
                    fail(AssetErrorCode::InvalidFormat, {}, "journal version encoding failed"));
            }
            bytes.insert(bytes.end(), header.bytes().begin(), header.bytes().end());
            bytes.insert(bytes.end(), writer.bytes().begin(), writer.bytes().end());
            return AssetResult<std::vector<std::uint8_t>>(std::move(bytes));
        }

        AssetResult<Journal> decode_journal(const std::vector<std::uint8_t>& bytes)
        {
            if (bytes.size() < 12u ||
                !std::equal(std::begin(k_journal_magic), std::end(k_journal_magic), bytes.begin()))
            {
                return AssetResult<Journal>(fail(AssetErrorCode::InvalidFormat, {}, "invalid asset journal"));
            }
            std::vector<std::uint8_t> body(bytes.begin() + 8u, bytes.end());
            ValueReader reader(body);
            std::uint32_t version = 0;
            Journal journal;
            if (!reader.read_uint32(version).succeeded() || version != k_journal_version)
            {
                return AssetResult<Journal>(fail(AssetErrorCode::InvalidFormat, {}, "unsupported asset journal"));
            }
            for (std::uint8_t& byte : journal.id.bytes)
            {
                if (!reader.read_uint8(byte).succeeded())
                {
                    return AssetResult<Journal>(fail(AssetErrorCode::InvalidFormat, {}, "truncated journal ID"));
                }
            }
            if (!reader.read_bool(journal.had_asset).succeeded() || !reader.read_bool(journal.had_meta).succeeded() ||
                !reader.read_bool(journal.has_meta).succeeded() || !reader.read_bool(journal.deleting).succeeded() ||
                !read_hash(reader, journal.old_asset_hash).succeeded() ||
                !read_hash(reader, journal.old_meta_hash).succeeded() ||
                !read_hash(reader, journal.new_asset_hash).succeeded() ||
                !read_hash(reader, journal.new_meta_hash).succeeded() || !reader.at_end() || !journal.id.valid())
            {
                return AssetResult<Journal>(fail(AssetErrorCode::InvalidFormat, {}, "invalid journal contents"));
            }
            return AssetResult<Journal>(std::move(journal));
        }

        struct MoveJournal
        {
            AssetId id;
            VirtualPath destination;
            bool has_meta = false;
            Sha256Hash asset_hash{};
            Sha256Hash meta_hash{};
        };

        AssetResult<std::vector<std::uint8_t>> encode_move_journal(const MoveJournal& journal)
        {
            ValueWriter writer;
            for (std::uint8_t byte : journal.id.bytes)
            {
                if (!writer.write_uint8(byte).succeeded())
                {
                    return AssetResult<std::vector<std::uint8_t>>(
                        fail(AssetErrorCode::InvalidFormat, journal.destination, "move journal ID failed"));
                }
            }
            if (!writer.write_utf8(journal.destination.utf8()).succeeded() ||
                !writer.write_bool(journal.has_meta).succeeded() ||
                !write_hash(writer, journal.asset_hash).succeeded() ||
                !write_hash(writer, journal.meta_hash).succeeded())
            {
                return AssetResult<std::vector<std::uint8_t>>(
                    fail(AssetErrorCode::InvalidFormat, journal.destination, "move journal encoding failed"));
            }
            std::vector<std::uint8_t> bytes{'T', 'O', 'Y', '3', 'D', 'M', 'O', 'V'};
            bytes.insert(bytes.end(), writer.bytes().begin(), writer.bytes().end());
            return AssetResult<std::vector<std::uint8_t>>(std::move(bytes));
        }

        AssetResult<MoveJournal> decode_move_journal(const std::vector<std::uint8_t>& bytes)
        {
            constexpr std::uint8_t magic[8] = {'T', 'O', 'Y', '3', 'D', 'M', 'O', 'V'};
            if (bytes.size() < 8u || !std::equal(std::begin(magic), std::end(magic), bytes.begin()))
            {
                return AssetResult<MoveJournal>(fail(AssetErrorCode::InvalidFormat, {}, "invalid move journal"));
            }
            std::vector<std::uint8_t> body(bytes.begin() + 8u, bytes.end());
            ValueReader reader(body);
            MoveJournal journal;
            std::string destination;
            for (std::uint8_t& byte : journal.id.bytes)
            {
                if (!reader.read_uint8(byte).succeeded())
                {
                    return AssetResult<MoveJournal>(fail(AssetErrorCode::InvalidFormat, {}, "truncated move journal"));
                }
            }
            if (!reader.read_utf8(destination).succeeded() || !reader.read_bool(journal.has_meta).succeeded() ||
                !read_hash(reader, journal.asset_hash).succeeded() ||
                !read_hash(reader, journal.meta_hash).succeeded() || !reader.at_end() || !journal.id.valid())
            {
                return AssetResult<MoveJournal>(
                    fail(AssetErrorCode::InvalidFormat, {}, "invalid move journal contents"));
            }
            const auto path = VirtualPath::parse(destination);
            if (!path.succeeded() || !paths_for(path.value()).succeeded())
            {
                return AssetResult<MoveJournal>(fail(AssetErrorCode::InvalidFormat, {}, "invalid move destination"));
            }
            journal.destination = path.value();
            return AssetResult<MoveJournal>(std::move(journal));
        }

        AssetStatus clean_staging(FileSystem& files, const PairPaths& paths, const Journal& journal)
        {
            AssetStatus status = remove_owned(files, paths.asset_stage, journal.new_asset_hash);
            if (status.succeeded() && journal.has_meta)
            {
                status = remove_owned(files, paths.meta_stage, journal.new_meta_hash);
            }
            return status;
        }
    } // namespace

    // --------------------------------------------------------------------------
    // AssetPairStore: Editor-owned pair publication and interrupted-write recovery
    // --------------------------------------------------------------------------
    AssetPairStore::AssetPairStore(const TypeRegistry& types, FileSystem& files) : types_(types), files_(files)
    {
    }

    AssetStatus AssetPairStore::publish(const VirtualPath& path, const AssetPairBytes& pair, FilePublishMode mode)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto paths = paths_for(path);
        if (!paths.succeeded())
        {
            return paths.status();
        }
        const auto parsed = decode_asset_yaml(types_, pair.asset);
        if (!parsed.succeeded() || pair.has_meta != parsed.value().has_meta ||
            !asset_descriptor_accepts_type(asset_descriptor_kind(path), parsed.value().index.root_type) ||
            (asset_descriptor_kind(path) == AssetDescriptorKind::Scene && pair.has_meta))
        {
            return fail(AssetErrorCode::InvalidFormat, path, "asset pair candidate description is invalid");
        }
        if (pair.has_meta)
        {
            const auto meta = decode_asset_meta(pair.meta);
            if (!meta.succeeded() || !(meta.value().asset_id == parsed.value().index.asset_id) ||
                pair.meta.size() != parsed.value().meta_size || sha256(pair.meta) != parsed.value().meta_hash)
            {
                return fail(AssetErrorCode::InvalidFormat, path, "asset pair candidate meta is invalid");
            }
            for (const std::string& name : parsed.value().meta_segments)
            {
                const auto found = std::find_if(meta.value().segments.begin(), meta.value().segments.end(),
                                                [&name](const AssetSegmentData& segment)
                                                {
                                                    return segment.name == name && segment.required;
                                                });
                if (found == meta.value().segments.end())
                {
                    return fail(AssetErrorCode::InvalidFormat, path,
                                "asset pair candidate is missing a required meta segment");
                }
            }
            for (const AssetSegmentData& segment : meta.value().segments)
            {
                if (segment.required &&
                    std::find(parsed.value().meta_segments.begin(), parsed.value().meta_segments.end(), segment.name) ==
                        parsed.value().meta_segments.end())
                {
                    return fail(AssetErrorCode::InvalidFormat, path,
                                "asset pair candidate has an undeclared required meta segment");
                }
            }
        }
        else if (!pair.meta.empty())
        {
            return fail(AssetErrorCode::InvalidFormat, path, "descriptive asset has meta bytes");
        }

        for (const VirtualPath* temporary :
             {&paths.value().journal, &paths.value().asset_stage, &paths.value().meta_stage,
              &paths.value().asset_backup, &paths.value().meta_backup})
        {
            const auto found = exists(files_, *temporary);
            if (!found.succeeded())
            {
                return found.status();
            }
            if (found.value())
            {
                return fail(AssetErrorCode::Conflict, *temporary,
                            "unfinished asset transaction must be recovered first");
            }
        }
        const auto existing = exists(files_, path);
        if (!existing.succeeded())
        {
            return existing.status();
        }
        if ((mode == FilePublishMode::CreateNew && existing.value()) ||
            (mode == FilePublishMode::Replace && !existing.value()))
        {
            return fail(AssetErrorCode::Conflict, path, "asset publication mode conflicts with disk state");
        }
        const auto existing_meta = exists(files_, paths.value().meta);
        if (!existing_meta.succeeded())
        {
            return existing_meta.status();
        }
        if (mode == FilePublishMode::CreateNew && existing_meta.value())
        {
            return fail(AssetErrorCode::Conflict, paths.value().meta, "orphan meta blocks asset creation");
        }

        Journal journal;
        journal.id = parsed.value().index.asset_id;
        journal.had_asset = existing.value();
        journal.had_meta = existing_meta.value();
        journal.has_meta = pair.has_meta;
        journal.new_asset_hash = sha256(pair.asset);
        if (pair.has_meta)
        {
            journal.new_meta_hash = sha256(pair.meta);
        }
        if (mode == FilePublishMode::Replace)
        {
            const auto old = read_asset_pair(types_, files_, path);
            if (!old.succeeded())
            {
                return old.status();
            }
            if (!(old.value().description.index.asset_id == journal.id))
            {
                return fail(AssetErrorCode::Conflict, path, "asset identity changed before replacement");
            }
            const auto old_asset_hash = file_hash(files_, path);
            if (!old_asset_hash.succeeded())
            {
                return old_asset_hash.status();
            }
            journal.old_asset_hash = old_asset_hash.value();
            if (journal.had_meta)
            {
                const auto old_meta_hash = file_hash(files_, paths.value().meta);
                if (!old_meta_hash.succeeded())
                {
                    return old_meta_hash.status();
                }
                journal.old_meta_hash = old_meta_hash.value();
            }
        }
        const auto journal_bytes = encode_journal(journal);
        if (!journal_bytes.succeeded())
        {
            return journal_bytes.status();
        }
        const FileStatus written =
            files_.write_binary_atomic(paths.value().journal, journal_bytes.value(), FilePublishMode::CreateNew);
        if (!written.succeeded())
        {
            return fail(AssetErrorCode::Io, paths.value().journal, "journal publication failed", written);
        }
        auto step = [&](const FileStatus& file, const VirtualPath& affected) -> AssetStatus
        {
            return file.succeeded() ? AssetStatus::success()
                                    : fail(AssetErrorCode::Io, affected, "asset transaction write failed", file);
        };
        AssetStatus status =
            step(files_.write_binary_atomic(paths.value().asset_stage, pair.asset, FilePublishMode::CreateNew),
                 paths.value().asset_stage);
        if (status.succeeded() && pair.has_meta)
        {
            status = step(files_.write_binary_atomic(paths.value().meta_stage, pair.meta, FilePublishMode::CreateNew),
                          paths.value().meta_stage);
        }
        if (status.succeeded() && journal.had_asset)
        {
            status = rename_file(files_, path, paths.value().asset_backup);
        }
        if (status.succeeded() && journal.had_meta)
        {
            status = rename_file(files_, paths.value().meta, paths.value().meta_backup);
        }
        if (status.succeeded() && pair.has_meta)
        {
            status = rename_file(files_, paths.value().meta_stage, paths.value().meta);
        }
        if (status.succeeded())
        {
            status = rename_file(files_, paths.value().asset_stage, path);
        }
        if (!status.succeeded())
        {
            const AssetStatus restored = recover_locked(path);
            return restored.succeeded() ? status : restored;
        }
        // The descriptor rename is the commit point. Cleanup can be retried on startup.
        const AssetStatus cleaned = recover_locked(path);
        return cleaned.succeeded() ? AssetStatus::success()
                                   : AssetStatus{AssetErrorCode::None,
                                                 journal.id,
                                                 path.utf8(),
                                                 {},
                                                 {},
                                                 "asset committed; transaction cleanup will retry on startup",
                                                 {}};
    }

    AssetStatus AssetPairStore::recover(const VirtualPath& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return recover_locked(path);
    }

    AssetResult<AssetPair> AssetPairStore::read(const VirtualPath& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return read_asset_pair(types_, files_, path);
    }

    AssetStatus AssetPairStore::remove(const VirtualPath& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto paths = paths_for(path);
        if (!paths.succeeded())
        {
            return paths.status();
        }
        const auto pending = exists(files_, paths.value().journal);
        if (!pending.succeeded())
        {
            return pending.status();
        }
        if (pending.value())
        {
            return fail(AssetErrorCode::Conflict, path, "unfinished asset transaction must be recovered first");
        }
        for (const VirtualPath* temporary : {&paths.value().asset_stage, &paths.value().meta_stage,
                                             &paths.value().asset_backup, &paths.value().meta_backup})
        {
            const auto found = exists(files_, *temporary);
            if (!found.succeeded())
            {
                return found.status();
            }
            if (found.value())
            {
                return fail(AssetErrorCode::Conflict, *temporary, "asset transaction temporary path already exists");
            }
        }
        const auto pair = read_asset_pair(types_, files_, path);
        if (!pair.succeeded())
        {
            return pair.status();
        }
        Journal journal;
        journal.id = pair.value().description.index.asset_id;
        journal.had_asset = true;
        journal.had_meta = pair.value().description.has_meta;
        journal.deleting = true;
        const auto asset_hash = file_hash(files_, path);
        if (!asset_hash.succeeded())
        {
            return asset_hash.status();
        }
        journal.old_asset_hash = asset_hash.value();
        if (journal.had_meta)
        {
            const auto meta_hash = file_hash(files_, paths.value().meta);
            if (!meta_hash.succeeded())
            {
                return meta_hash.status();
            }
            journal.old_meta_hash = meta_hash.value();
        }
        const auto encoded = encode_journal(journal);
        if (!encoded.succeeded())
        {
            return encoded.status();
        }
        const FileStatus written =
            files_.write_binary_atomic(paths.value().journal, encoded.value(), FilePublishMode::CreateNew);
        if (!written.succeeded())
        {
            return fail(AssetErrorCode::Io, path, "asset deletion journal failed", written);
        }
        AssetStatus status = rename_file(files_, path, paths.value().asset_backup);
        if (status.succeeded() && journal.had_meta)
        {
            status = rename_file(files_, paths.value().meta, paths.value().meta_backup);
        }
        if (!status.succeeded())
        {
            const AssetStatus restored = recover_locked(path);
            return restored.succeeded() ? status : restored;
        }
        return recover_locked(path);
    }

    AssetResult<AssetId> AssetPairStore::copy(const VirtualPath& source, const VirtualPath& destination)
    {
        if (asset_descriptor_kind(source) != asset_descriptor_kind(destination))
        {
            return AssetResult<AssetId>(
                fail(AssetErrorCode::TypeMismatch, destination, "copy must preserve descriptor kind"));
        }
        if (source == destination)
        {
            return AssetResult<AssetId>(fail(AssetErrorCode::Conflict, source, "copy destination equals source"));
        }
        const auto original = read(source);
        if (!original.succeeded())
        {
            return AssetResult<AssetId>(original.status());
        }
        AssetId id;
        if (!AssetId::try_generate(id))
        {
            return AssetResult<AssetId>(
                fail(AssetErrorCode::InvalidState, destination, "could not allocate copied Asset ID"));
        }
        AssetFileIndex index = original.value().description.index;
        index.asset_id = id;
        const auto encoded = encode_asset_pair(types_, std::move(index), original.value().description.type_data,
                                               original.value().meta.segments);
        if (!encoded.succeeded())
        {
            return AssetResult<AssetId>(encoded.status());
        }
        const AssetStatus published = publish(destination, encoded.value(), FilePublishMode::CreateNew);
        if (!published.succeeded())
        {
            return AssetResult<AssetId>(published);
        }
        return AssetResult<AssetId>(id);
    }

    AssetStatus AssetPairStore::move(const VirtualPath& source, const VirtualPath& destination)
    {
        if (asset_descriptor_kind(source) != asset_descriptor_kind(destination))
        {
            return fail(AssetErrorCode::TypeMismatch, destination, "move must preserve descriptor kind");
        }
        if (source == destination)
        {
            return fail(AssetErrorCode::Conflict, source, "move destination equals source");
        }
        const auto source_paths = paths_for(source);
        const auto destination_paths = paths_for(destination);
        if (!source_paths.succeeded())
        {
            return source_paths.status();
        }
        if (!destination_paths.succeeded())
        {
            return destination_paths.status();
        }
        const auto destination_asset = exists(files_, destination);
        const auto destination_meta = exists(files_, destination_paths.value().meta);
        if (!destination_asset.succeeded())
        {
            return destination_asset.status();
        }
        if (!destination_meta.succeeded())
        {
            return destination_meta.status();
        }
        if (destination_asset.value() || destination_meta.value())
        {
            return fail(AssetErrorCode::Conflict, destination, "asset move destination already exists");
        }
        const auto pending = exists(files_, source_paths.value().move_journal);
        if (!pending.succeeded())
        {
            return pending.status();
        }
        if (pending.value())
        {
            return fail(AssetErrorCode::Conflict, source, "unfinished asset move must be recovered first");
        }
        const auto original = read(source);
        if (!original.succeeded())
        {
            return original.status();
        }
        const auto candidate =
            encode_asset_pair(types_, original.value().description.index, original.value().description.type_data,
                              original.value().meta.segments);
        if (!candidate.succeeded())
        {
            return candidate.status();
        }
        MoveJournal journal;
        journal.id = original.value().description.index.asset_id;
        journal.destination = destination;
        journal.has_meta = candidate.value().has_meta;
        journal.asset_hash = sha256(candidate.value().asset);
        if (journal.has_meta)
        {
            journal.meta_hash = sha256(candidate.value().meta);
        }
        const auto encoded = encode_move_journal(journal);
        if (!encoded.succeeded())
        {
            return encoded.status();
        }
        const FileStatus written =
            files_.write_binary_atomic(source_paths.value().move_journal, encoded.value(), FilePublishMode::CreateNew);
        if (!written.succeeded())
        {
            return fail(AssetErrorCode::Io, source, "move journal publication failed", written);
        }
        const AssetStatus published = publish(destination, candidate.value(), FilePublishMode::CreateNew);
        if (!published.succeeded())
        {
            const AssetStatus recovered = recover_move(source);
            if (!recovered.succeeded())
            {
                return recovered;
            }
            const auto committed = exists(files_, destination);
            if (!committed.succeeded())
            {
                return committed.status();
            }
            return committed.value() ? AssetStatus::success() : published;
        }
        return recover_move(source);
    }

    AssetStatus AssetPairStore::recover_locked(const VirtualPath& path)
    {
        const auto paths = paths_for(path);
        if (!paths.succeeded())
        {
            return paths.status();
        }
        const auto present = exists(files_, paths.value().journal);
        if (!present.succeeded())
        {
            return present.status();
        }
        if (!present.value())
        {
            return AssetStatus::success();
        }
        const auto bytes = files_.read_binary(paths.value().journal, 1024u);
        if (!bytes.succeeded())
        {
            return fail(AssetErrorCode::Io, paths.value().journal, "journal read failed", bytes.status());
        }
        const auto decoded = decode_journal(bytes.value());
        if (!decoded.succeeded())
        {
            return decoded.status();
        }
        const Journal& journal = decoded.value();
        const auto current_asset = exists(files_, path);
        const auto current_meta = exists(files_, paths.value().meta);
        if (!current_asset.succeeded())
        {
            return current_asset.status();
        }
        if (!current_meta.succeeded())
        {
            return current_meta.status();
        }
        bool committed = journal.deleting && !current_asset.value() && !current_meta.value();
        if (current_asset.value())
        {
            const auto hash = file_hash(files_, path);
            if (!hash.succeeded())
            {
                return hash.status();
            }
            committed = hash.value() == journal.new_asset_hash;
        }
        if (committed)
        {
            if (journal.has_meta != current_meta.value())
            {
                committed = false;
            }
            else if (journal.has_meta)
            {
                const AssetStatus checked = verify_hash(files_, paths.value().meta, journal.new_meta_hash);
                committed = checked.succeeded();
            }
        }
        if (committed)
        {
            AssetStatus status = clean_staging(files_, paths.value(), journal);
            if (status.succeeded() && journal.had_asset)
            {
                status = remove_owned(files_, paths.value().asset_backup, journal.old_asset_hash);
            }
            if (status.succeeded() && journal.had_meta)
            {
                status = remove_owned(files_, paths.value().meta_backup, journal.old_meta_hash);
            }
            if (!status.succeeded())
            {
                return status;
            }
            const FileStatus removed = files_.remove_file(paths.value().journal);
            return removed.succeeded()
                       ? AssetStatus::success()
                       : fail(AssetErrorCode::Io, paths.value().journal, "journal cleanup failed", removed);
        }

        const auto backup_asset = exists(files_, paths.value().asset_backup);
        const auto backup_meta = exists(files_, paths.value().meta_backup);
        if (!backup_asset.succeeded())
        {
            return backup_asset.status();
        }
        if (!backup_meta.succeeded())
        {
            return backup_meta.status();
        }
        // Validate every file before removing either side of the visible pair.
        if (backup_asset.value())
        {
            if (!journal.had_asset)
            {
                return fail(AssetErrorCode::Conflict, paths.value().asset_backup, "unexpected asset backup");
            }
            const AssetStatus checked = verify_hash(files_, paths.value().asset_backup, journal.old_asset_hash);
            if (!checked.succeeded())
            {
                return checked;
            }
        }
        if (backup_meta.value())
        {
            if (!journal.had_meta)
            {
                return fail(AssetErrorCode::Conflict, paths.value().meta_backup, "unexpected meta backup");
            }
            const AssetStatus checked = verify_hash(files_, paths.value().meta_backup, journal.old_meta_hash);
            if (!checked.succeeded())
            {
                return checked;
            }
        }
        bool remove_new_asset = false;
        bool remove_new_meta = false;
        if (current_asset.value())
        {
            const auto hash = file_hash(files_, path);
            if (!hash.succeeded())
            {
                return hash.status();
            }
            if (hash.value() == journal.new_asset_hash)
            {
                if (journal.had_asset && !backup_asset.value())
                {
                    return fail(AssetErrorCode::Conflict, path, "old asset backup is missing");
                }
                remove_new_asset = true;
            }
            else if (!journal.had_asset || hash.value() != journal.old_asset_hash || backup_asset.value())
            {
                return fail(AssetErrorCode::Conflict, path, "cannot identify asset version for rollback");
            }
        }
        else if (journal.had_asset && !backup_asset.value())
        {
            return fail(AssetErrorCode::Conflict, path, "old asset version is missing");
        }
        if (current_meta.value())
        {
            const auto hash = file_hash(files_, paths.value().meta);
            if (!hash.succeeded())
            {
                return hash.status();
            }
            if (journal.has_meta && hash.value() == journal.new_meta_hash)
            {
                if (journal.had_meta && !backup_meta.value())
                {
                    return fail(AssetErrorCode::Conflict, paths.value().meta, "old meta backup is missing");
                }
                remove_new_meta = true;
            }
            else if (!journal.had_meta || hash.value() != journal.old_meta_hash || backup_meta.value())
            {
                return fail(AssetErrorCode::Conflict, paths.value().meta, "cannot identify meta version for rollback");
            }
        }
        else if (journal.had_meta && !backup_meta.value())
        {
            return fail(AssetErrorCode::Conflict, paths.value().meta, "old meta version is missing");
        }
        if (remove_new_asset)
        {
            const AssetStatus removed = remove_owned(files_, path, journal.new_asset_hash);
            if (!removed.succeeded())
            {
                return removed;
            }
        }
        if (remove_new_meta)
        {
            const AssetStatus removed = remove_owned(files_, paths.value().meta, journal.new_meta_hash);
            if (!removed.succeeded())
            {
                return removed;
            }
        }
        if (backup_asset.value())
        {
            const AssetStatus restored = rename_file(files_, paths.value().asset_backup, path);
            if (!restored.succeeded())
            {
                return restored;
            }
        }
        if (backup_meta.value())
        {
            const AssetStatus restored = rename_file(files_, paths.value().meta_backup, paths.value().meta);
            if (!restored.succeeded())
            {
                return restored;
            }
        }
        const AssetStatus cleaned = clean_staging(files_, paths.value(), journal);
        if (!cleaned.succeeded())
        {
            return cleaned;
        }
        const FileStatus removed = files_.remove_file(paths.value().journal);
        return removed.succeeded() ? AssetStatus::success()
                                   : fail(AssetErrorCode::Io, paths.value().journal, "journal cleanup failed", removed);
    }

    AssetStatus AssetPairStore::recover_tree(const VirtualPath& root)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const AssetStatus recovered = recover_tree_locked(root, 0u);
            if (!recovered.succeeded())
            {
                return recovered;
            }
        }
        return recover_moves_tree(root, 0u);
    }

    AssetStatus AssetPairStore::recover_tree_locked(const VirtualPath& root, std::size_t depth)
    {
        if (depth > k_max_depth)
        {
            return fail(AssetErrorCode::TooLarge, root, "asset recovery directory depth exceeded");
        }
        const auto listed = files_.enumerate(root);
        if (!listed.succeeded())
        {
            return fail(AssetErrorCode::Io, root, "asset recovery enumeration failed", listed.status());
        }
        for (const VirtualDirectoryEntry& entry : listed.value())
        {
            const auto child = VirtualPath::parse(root.utf8() + "/" + entry.name);
            if (!child.succeeded())
            {
                return fail(AssetErrorCode::InvalidFormat, root, "invalid recovery entry name");
            }
            if (entry.type == FileType::Directory)
            {
                const AssetStatus nested = recover_tree_locked(child.value(), depth + 1u);
                if (!nested.succeeded())
                {
                    return nested;
                }
            }
            else if (entry.type == FileType::File && entry.name.size() > 10u &&
                     entry.name.compare(entry.name.size() - 4u, 4u, ".txn") == 0)
            {
                const auto asset =
                    VirtualPath::parse(child.value().utf8().substr(0u, child.value().utf8().size() - 4u));
                if (!asset.succeeded() || asset_descriptor_kind(asset.value()) == AssetDescriptorKind::Invalid)
                {
                    return fail(AssetErrorCode::InvalidFormat, child.value(), "invalid journal name");
                }
                const AssetStatus recovered = recover_locked(asset.value());
                if (!recovered.succeeded())
                {
                    return recovered;
                }
            }
        }
        return AssetStatus::success();
    }

    AssetStatus AssetPairStore::recover_move(const VirtualPath& source)
    {
        const auto paths = paths_for(source);
        if (!paths.succeeded())
        {
            return paths.status();
        }
        const auto bytes = files_.read_binary(paths.value().move_journal, 4096u);
        if (!bytes.succeeded())
        {
            return fail(AssetErrorCode::Io, paths.value().move_journal, "move journal read failed", bytes.status());
        }
        const auto journal = decode_move_journal(bytes.value());
        if (!journal.succeeded())
        {
            return journal.status();
        }
        if (journal.value().destination == source)
        {
            return fail(AssetErrorCode::InvalidFormat, source, "move journal points to itself");
        }
        const auto destination_paths = paths_for(journal.value().destination);
        if (!destination_paths.succeeded())
        {
            return destination_paths.status();
        }
        const auto destination = exists(files_, journal.value().destination);
        const auto source_exists = exists(files_, source);
        if (!destination.succeeded())
        {
            return destination.status();
        }
        if (!source_exists.succeeded())
        {
            return source_exists.status();
        }
        if (!destination.value())
        {
            if (!source_exists.value())
            {
                return fail(AssetErrorCode::Conflict, source, "both source and destination of asset move are missing");
            }
            const FileStatus removed = files_.remove_file(paths.value().move_journal);
            return removed.succeeded()
                       ? AssetStatus::success()
                       : fail(AssetErrorCode::Io, paths.value().move_journal, "move journal cleanup failed", removed);
        }
        const AssetStatus checked = verify_hash(files_, journal.value().destination, journal.value().asset_hash);
        if (!checked.succeeded())
        {
            return checked;
        }
        const auto meta_exists = exists(files_, destination_paths.value().meta);
        if (!meta_exists.succeeded())
        {
            return meta_exists.status();
        }
        if (meta_exists.value() != journal.value().has_meta)
        {
            return fail(AssetErrorCode::Conflict, journal.value().destination, "move destination pair is incomplete");
        }
        if (journal.value().has_meta)
        {
            const AssetStatus meta_checked =
                verify_hash(files_, destination_paths.value().meta, journal.value().meta_hash);
            if (!meta_checked.succeeded())
            {
                return meta_checked;
            }
        }
        if (source_exists.value())
        {
            const auto original = read(source);
            if (!original.succeeded())
            {
                return original.status();
            }
            if (!(original.value().description.index.asset_id == journal.value().id))
            {
                return fail(AssetErrorCode::Conflict, source, "move source identity changed");
            }
            const AssetStatus removed = remove(source);
            if (!removed.succeeded())
            {
                return removed;
            }
        }
        const FileStatus cleared = files_.remove_file(paths.value().move_journal);
        return cleared.succeeded()
                   ? AssetStatus::success()
                   : fail(AssetErrorCode::Io, paths.value().move_journal, "move journal cleanup failed", cleared);
    }

    AssetStatus AssetPairStore::recover_moves_tree(const VirtualPath& root, std::size_t depth)
    {
        if (depth > k_max_depth)
        {
            return fail(AssetErrorCode::TooLarge, root, "asset move recovery directory depth exceeded");
        }
        const auto listed = files_.enumerate(root);
        if (!listed.succeeded())
        {
            return fail(AssetErrorCode::Io, root, "asset move recovery enumeration failed", listed.status());
        }
        for (const VirtualDirectoryEntry& entry : listed.value())
        {
            const auto child = VirtualPath::parse(root.utf8() + "/" + entry.name);
            if (!child.succeeded())
            {
                return fail(AssetErrorCode::InvalidFormat, root, "invalid move recovery entry name");
            }
            if (entry.type == FileType::Directory)
            {
                const AssetStatus nested = recover_moves_tree(child.value(), depth + 1u);
                if (!nested.succeeded())
                {
                    return nested;
                }
            }
            else if (entry.type == FileType::File && entry.name.size() > 11u &&
                     entry.name.compare(entry.name.size() - 5u, 5u, ".move") == 0)
            {
                const auto source =
                    VirtualPath::parse(child.value().utf8().substr(0u, child.value().utf8().size() - 5u));
                if (!source.succeeded() || asset_descriptor_kind(source.value()) == AssetDescriptorKind::Invalid)
                {
                    return fail(AssetErrorCode::InvalidFormat, child.value(), "invalid asset move journal name");
                }
                const AssetStatus recovered = recover_move(source.value());
                if (!recovered.succeeded())
                {
                    return recovered;
                }
            }
        }
        return AssetStatus::success();
    }
} // namespace toy3d
