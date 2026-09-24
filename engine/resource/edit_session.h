#pragma once

#include "asset_index.h"
#include "property_path.h"

#include <functional>
#include <thread>
#include <utility>

namespace toy3d
{
    enum class EditChangeKind
    {
        Setter,
        Reimport,
        Cook,
        ReplaceCandidate
    };

    struct EditPatch
    {
        PropertyPath path;
        std::vector<std::uint8_t> new_value;
        EditChangeKind kind = EditChangeKind::Setter;
    };

    struct PropertyChange
    {
        PropertyPath path;
        std::vector<std::uint8_t> old_value;
        std::vector<std::uint8_t> new_value;
    };

    struct EditRecord
    {
        AssetId asset_id;
        std::vector<PropertyChange> changes;
        EditChangeKind kind = EditChangeKind::Setter;
    };

    template <typename T> class EditSession
    {
      public:
        using Validator = std::function<AssetStatus(const T&)>;
        using PreviewPrepare = std::function<AssetStatus(const T&, EditChangeKind)>;
        using PreviewNotify = std::function<void(const EditRecord&)>;

        EditSession(const TypeRegistry& types, const TypeDesc& type, AssetId id, VirtualPath path,
                    T initial, Validator validator, const AssetIndex* index = nullptr,
                    PreviewPrepare prepare = {}, PreviewNotify notify = {})
            : types_(types), type_(type), id_(id), path_(std::move(path)), current_(std::move(initial)),
              validator_(std::move(validator)), index_(index), prepare_(std::move(prepare)),
              notify_(std::move(notify)), owner_thread_(std::this_thread::get_id())
        {
            ValueWriter writer;
            if (encode_value(writer, current_).succeeded()) saved_bytes_ = writer.bytes();
        }

        const T& value() const { return current_; }
        bool dirty() const
        {
            ValueWriter writer;
            return !encode_value(writer, current_).succeeded() || writer.bytes() != saved_bytes_;
        }
        std::size_t undo_count() const { return cursor_; }
        std::size_t redo_count() const { return history_.size() - cursor_; }

        AssetStatus bind_published(const FileSystem& files, AssetFileLimits file_limits = {},
                                   ValueLimits value_limits = {})
        {
            AssetStatus thread = check_thread();
            if (!thread.succeeded()) return thread;
            auto inspected = inspect_asset(files, path_, file_limits);
            if (!inspected.succeeded()) return inspected.status();
            if (!(inspected.value().asset_id == id_) || inspected.value().root_type != type_.name)
                return problem(AssetErrorCode::TypeMismatch, {}, "published asset identity or type differs");
            const AssetSegment* typed = nullptr;
            for (const AssetSegment& segment : inspected.value().segments)
                if (segment.name == "type_data") typed = &segment;
            if (typed == nullptr) return problem(AssetErrorCode::InvalidFormat, {}, "type_data is missing");
            auto bytes = read_asset_segment(files, path_, id_, *typed, value_limits.max_bytes);
            if (!bytes.succeeded()) return bytes.status();
            published_type_bytes_ = bytes.value();
            bound_ = true;
            return AssetStatus::success();
        }

        AssetResult<EditRecord> apply_edit(const std::vector<EditPatch>& patches,
                                           ValueLimits limits = {})
        {
            AssetStatus thread = check_thread();
            if (!thread.succeeded()) return AssetResult<EditRecord>(thread);
            if (!types_.frozen() || patches.empty())
                return AssetResult<EditRecord>(problem(AssetErrorCode::InvalidState, {},
                    "frozen schema and at least one patch required"));
            ValueWriter original_writer(limits);
            ValueStatus encoded = encode_value(original_writer, current_);
            if (!encoded.succeeded())
                return AssetResult<EditRecord>(problem(AssetErrorCode::Value, encoded.property_path,
                    encoded.message.c_str()));
            std::vector<std::uint8_t> candidate_bytes = original_writer.bytes();
            EditRecord record;
            record.asset_id = id_;
            for (const EditPatch& patch : patches)
            {
                auto accessed = access_property(types_, type_, candidate_bytes, patch.path,
                                                &patch.new_value, limits);
                if (!accessed.succeeded())
                {
                    AssetStatus status = accessed.status();
                    status.asset_id = id_;
                    status.virtual_path = path_.utf8();
                    return AssetResult<EditRecord>(status);
                }
                if (accessed.value().value_type.kind == ValueKind::AssetRef)
                {
                    ValueReader reader(patch.new_value, limits);
                    AssetRef reference;
                    const ValueStatus decoded = decode_value(reader, reference);
                    if (!decoded.succeeded() || !reader.at_end())
                        return AssetResult<EditRecord>(problem(AssetErrorCode::Value,
                            format_property_path(patch.path), "invalid asset reference value"));
                    if (index_ == nullptr)
                        return AssetResult<EditRecord>(problem(AssetErrorCode::MissingReference,
                            format_property_path(patch.path),
                            "asset reference index is unavailable"));
                    AssetStatus resolved = index_->resolve(reference, format_property_path(patch.path));
                    if (!resolved.succeeded())
                    {
                        resolved.asset_id = id_;
                        resolved.virtual_path = path_.utf8();
                        return AssetResult<EditRecord>(resolved);
                    }
                }
                record.changes.push_back({patch.path, accessed.value().value_bytes, patch.new_value});
                candidate_bytes = accessed.value().root_bytes;
                if (static_cast<int>(patch.kind) > static_cast<int>(record.kind)) record.kind = patch.kind;
            }
            ValueReader candidate_reader(candidate_bytes, limits);
            T candidate{};
            const ValueStatus decoded = decode_value(candidate_reader, candidate);
            if (!decoded.succeeded() || !candidate_reader.at_end())
                return AssetResult<EditRecord>(problem(AssetErrorCode::Value, decoded.property_path,
                    decoded.succeeded() ? "edited value has trailing bytes" : decoded.message.c_str()));
            AssetStatus valid = validator_(candidate);
            if (!valid.succeeded())
            {
                valid.asset_id = id_;
                valid.virtual_path = path_.utf8();
                return AssetResult<EditRecord>(valid);
            }
            if (prepare_)
            {
                AssetStatus prepared = prepare_(candidate, record.kind);
                if (!prepared.succeeded())
                {
                    prepared.asset_id = id_;
                    prepared.virtual_path = path_.utf8();
                    return AssetResult<EditRecord>(prepared);
                }
            }
            if (candidate_bytes == original_writer.bytes())
            {
                record.changes.clear();
                return AssetResult<EditRecord>(std::move(record));
            }
            history_.resize(cursor_);
            history_.push_back({current_, candidate, original_writer.bytes(), candidate_bytes, record});
            ++cursor_;
            current_ = std::move(candidate);
            if (notify_) notify_(record);
            return AssetResult<EditRecord>(std::move(record));
        }

        AssetStatus undo()
        {
            AssetStatus thread = check_thread();
            if (!thread.succeeded()) return thread;
            if (cursor_ == 0) return problem(AssetErrorCode::InvalidState, {}, "undo stack is empty");
            const UndoState& state = history_[cursor_ - 1];
            if (prepare_)
            {
                AssetStatus prepared = prepare_(state.before, state.record.kind);
                if (!prepared.succeeded())
                {
                    prepared.asset_id = id_;
                    prepared.virtual_path = path_.utf8();
                    return prepared;
                }
            }
            current_ = state.before;
            --cursor_;
            if (notify_) notify_(state.record);
            return AssetStatus::success();
        }

        AssetStatus redo()
        {
            AssetStatus thread = check_thread();
            if (!thread.succeeded()) return thread;
            if (cursor_ >= history_.size()) return problem(AssetErrorCode::InvalidState, {}, "redo stack is empty");
            const UndoState& state = history_[cursor_];
            if (prepare_)
            {
                AssetStatus prepared = prepare_(state.after, state.record.kind);
                if (!prepared.succeeded())
                {
                    prepared.asset_id = id_;
                    prepared.virtual_path = path_.utf8();
                    return prepared;
                }
            }
            current_ = state.after;
            ++cursor_;
            if (notify_) notify_(state.record);
            return AssetStatus::success();
        }

        AssetStatus save(FileSystem& files, const SchemaMigrationRegistry& migrations,
                         AssetFileIndex index, std::vector<AssetSegmentData> extra = {},
                         AssetFileLimits file_limits = {}, ValueLimits value_limits = {})
        {
            AssetStatus thread = check_thread();
            if (!thread.succeeded()) return thread;
            if (!bound_) return problem(AssetErrorCode::InvalidState, {}, "edit session is not bound to a file");
            auto inspected = inspect_asset(files, path_, file_limits);
            if (!inspected.succeeded()) return inspected.status();
            const AssetSegment* typed = nullptr;
            for (const AssetSegment& segment : inspected.value().segments)
                if (segment.name == "type_data") typed = &segment;
            if (typed == nullptr) return problem(AssetErrorCode::InvalidFormat, {}, "type_data is missing");
            auto published = read_asset_segment(files, path_, id_, *typed, value_limits.max_bytes);
            if (!published.succeeded()) return published.status();
            if (published.value() != published_type_bytes_)
                return problem(AssetErrorCode::Conflict, {}, "published data changed since the edit session opened");
            AssetStatus status = save_asset(types_, migrations, files, path_, std::move(index), current_,
                                            validator_, std::move(extra), file_limits, value_limits);
            if (!status.succeeded()) return status;
            ValueWriter writer(value_limits);
            const ValueStatus encoded = encode_value(writer, current_);
            if (!encoded.succeeded()) return problem(AssetErrorCode::Value, encoded.property_path,
                                                     encoded.message.c_str());
            published_type_bytes_ = writer.bytes();
            saved_bytes_ = writer.bytes();
            return AssetStatus::success();
        }

      private:
        struct UndoState
        {
            T before;
            T after;
            std::vector<std::uint8_t> before_bytes;
            std::vector<std::uint8_t> after_bytes;
            EditRecord record;
        };

        AssetStatus problem(AssetErrorCode code, std::string property_path, const char* message) const
        {
            return {code, id_, path_.utf8(), {}, std::move(property_path), message, {}};
        }

        AssetStatus check_thread() const
        {
            return std::this_thread::get_id() == owner_thread_ ? AssetStatus::success() :
                   problem(AssetErrorCode::InvalidState, {}, "edit session accessed from another thread");
        }

        const TypeRegistry& types_;
        const TypeDesc& type_;
        AssetId id_;
        VirtualPath path_;
        T current_;
        Validator validator_;
        const AssetIndex* index_ = nullptr;
        PreviewPrepare prepare_;
        PreviewNotify notify_;
        std::thread::id owner_thread_;
        std::vector<UndoState> history_;
        std::size_t cursor_ = 0;
        std::vector<std::uint8_t> saved_bytes_;
        std::vector<std::uint8_t> published_type_bytes_;
        bool bound_ = false;
    };
} // namespace toy3d
