#include "file_system/file_system.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <map>
#include <utility>

namespace toy3d
{
    namespace
    {
        FileStatus file_system_error(FileErrorCode code, const char* operation, const std::string& virtual_path,
                                     std::string message)
        {
            FileStatus status;
            status.code = code;
            status.operation = operation;
            status.virtual_path = virtual_path;
            status.message = std::move(message);
            return status;
        }

        FileStatus with_virtual_path(FileStatus status, const VirtualPath& path)
        {
            status.virtual_path = path.utf8();
            return status;
        }

        std::string fold_ascii_case(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return text;
        }

        bool matches_mount(const VirtualPath& path, const VirtualPath& root)
        {
            if (root.is_root())
            {
                return true;
            }
            const std::string& candidate = path.utf8();
            const std::string& prefix = root.utf8();
            return candidate == prefix ||
                   (candidate.size() > prefix.size() && candidate.compare(0, prefix.size(), prefix) == 0 &&
                    candidate[prefix.size()] == '/');
        }

        FileResult<StorePath> relative_store_path(const VirtualPath& path, const VirtualPath& virtual_root,
                                                  const StorePath& store_root)
        {
            std::size_t start = virtual_root.is_root() ? 1 : virtual_root.utf8().size();
            if (start < path.utf8().size() && path.utf8()[start] == '/')
            {
                ++start;
            }
            const FileResult<StorePath> relative = StorePath::parse(path.utf8().substr(start));
            if (!relative.succeeded())
            {
                return relative;
            }
            return StorePath::join(store_root, relative.value());
        }

        bool is_valid_utf8(const std::string& text)
        {
            std::size_t index = 0;
            while (index < text.size())
            {
                const auto lead = static_cast<unsigned char>(text[index]);
                std::size_t continuation_count = 0;
                std::uint32_t code_point = 0;
                if (lead <= 0x7f)
                {
                    ++index;
                    continue;
                }
                if ((lead & 0xe0u) == 0xc0u)
                {
                    continuation_count = 1;
                    code_point = lead & 0x1fu;
                }
                else if ((lead & 0xf0u) == 0xe0u)
                {
                    continuation_count = 2;
                    code_point = lead & 0x0fu;
                }
                else if ((lead & 0xf8u) == 0xf0u)
                {
                    continuation_count = 3;
                    code_point = lead & 0x07u;
                }
                else
                {
                    return false;
                }
                if (index + continuation_count >= text.size())
                    return false;
                for (std::size_t offset = 1; offset <= continuation_count; ++offset)
                {
                    const auto next = static_cast<unsigned char>(text[index + offset]);
                    if ((next & 0xc0u) != 0x80u)
                        return false;
                    code_point = (code_point << 6u) | (next & 0x3fu);
                }
                const bool overlong = (continuation_count == 1 && code_point < 0x80u) ||
                                      (continuation_count == 2 && code_point < 0x800u) ||
                                      (continuation_count == 3 && code_point < 0x10000u);
                if (overlong || code_point > 0x10ffffu || (code_point >= 0xd800u && code_point <= 0xdfffu))
                    return false;
                index += continuation_count + 1;
            }
            return true;
        }

        class StoreOwnedFileHandle final : public FileHandle
        {
          public:
            StoreOwnedFileHandle(std::shared_ptr<FileStore> store, std::unique_ptr<FileHandle> handle)
                : store_(std::move(store)), handle_(std::move(handle))
            {
            }

            FileResult<std::uint64_t> size() const override { return handle_->size(); }
            FileResult<std::size_t> read(std::uint8_t* destination, std::size_t byte_count) override
            {
                return handle_->read(destination, byte_count);
            }
            FileResult<std::size_t> write(const std::uint8_t* source, std::size_t byte_count) override
            {
                return handle_->write(source, byte_count);
            }
            FileResult<std::uint64_t> tell() const override { return handle_->tell(); }
            FileStatus seek(std::uint64_t offset) override { return handle_->seek(offset); }
            FileResult<std::size_t> read_at(std::uint64_t offset, std::uint8_t* destination,
                                            std::size_t byte_count) const override
            {
                return handle_->read_at(offset, destination, byte_count);
            }
            FileStatus flush() override { return handle_->flush(); }
            FileStatus close() override { return handle_->close(); }

          private:
            std::shared_ptr<FileStore> store_;
            std::unique_ptr<FileHandle> handle_;
        };

        FileResult<StorePath> staging_path_for(const StorePath& destination, std::uint64_t sequence)
        {
            const std::size_t separator = destination.utf8().find_last_of('/');
            const std::string parent =
                separator == std::string::npos ? std::string() : destination.utf8().substr(0, separator);
            const std::string name = ".toy3d-staging-" + std::to_string(sequence);
            return StorePath::parse(parent.empty() ? name : parent + "/" + name);
        }
    } // namespace

    FileStatus FileSystem::add_mount(const FileMountDesc& descriptor)
    {
        if (frozen_)
        {
            return file_system_error(FileErrorCode::AccessDenied, "add_mount", descriptor.virtual_root.utf8(),
                                     "mount snapshot is frozen");
        }
        if (descriptor.version != file_mount_desc_version)
        {
            return file_system_error(FileErrorCode::Unsupported, "add_mount", descriptor.virtual_root.utf8(),
                                     "mount descriptor version is unsupported");
        }
        if (descriptor.virtual_root.empty() || descriptor.store == nullptr)
        {
            return file_system_error(FileErrorCode::InvalidPath, "add_mount", descriptor.virtual_root.utf8(),
                                     "mount root and store must be valid");
        }
        const FileStoreCapabilities capabilities = descriptor.store->capabilities();
        if (descriptor.access == MountAccess::ReadWrite && !capabilities.writable)
        {
            return file_system_error(FileErrorCode::ReadOnly, "add_mount", descriptor.virtual_root.utf8(),
                                     "writable mount requires a writable store");
        }
        if (descriptor.allow_enumeration && !capabilities.enumerable)
        {
            return file_system_error(FileErrorCode::Unsupported, "add_mount", descriptor.virtual_root.utf8(),
                                     "enumerable mount requires an enumerable store");
        }

        const std::string folded_root = fold_ascii_case(descriptor.virtual_root.utf8());
        for (const RegisteredMount& mount : mounts_)
        {
            const std::string& existing_root = mount.descriptor.virtual_root.utf8();
            if (fold_ascii_case(existing_root) == folded_root && existing_root != descriptor.virtual_root.utf8())
            {
                return file_system_error(FileErrorCode::AlreadyExists, "add_mount", descriptor.virtual_root.utf8(),
                                         "mount roots differing only by ASCII case are forbidden");
            }
            if (existing_root != descriptor.virtual_root.utf8())
            {
                continue;
            }
            if (mount.descriptor.priority == descriptor.priority)
            {
                return file_system_error(FileErrorCode::AlreadyExists, "add_mount", descriptor.virtual_root.utf8(),
                                         "overlay priorities must be unique within a mount root");
            }
            if (mount.descriptor.access == MountAccess::ReadWrite && descriptor.access == MountAccess::ReadWrite)
            {
                return file_system_error(FileErrorCode::AlreadyExists, "add_mount", descriptor.virtual_root.utf8(),
                                         "an overlay group may contain only one writable layer");
            }
        }
        mounts_.push_back({descriptor});
        return FileStatus::success();
    }

    FileStatus FileSystem::freeze()
    {
        if (frozen_)
            return FileStatus::success();
        std::sort(mounts_.begin(), mounts_.end(),
                  [](const RegisteredMount& lhs, const RegisteredMount& rhs)
                  {
                      const std::string& lhs_root = lhs.descriptor.virtual_root.utf8();
                      const std::string& rhs_root = rhs.descriptor.virtual_root.utf8();
                      if (lhs_root.size() != rhs_root.size())
                          return lhs_root.size() > rhs_root.size();
                      if (lhs_root != rhs_root)
                          return lhs_root < rhs_root;
                      return lhs.descriptor.priority > rhs.descriptor.priority;
                  });
        frozen_ = true;
        return FileStatus::success();
    }

    bool FileSystem::frozen() const
    {
        return frozen_;
    }

    FileResult<std::vector<FileSystem::RoutedLayer>> FileSystem::route_read(const VirtualPath& path,
                                                                            bool enumeration) const
    {
        if (!frozen_)
        {
            return FileResult<std::vector<RoutedLayer>>(
                file_system_error(FileErrorCode::InvalidState, "route", path.utf8(), "mount snapshot is not frozen"));
        }
        const RegisteredMount* first = nullptr;
        for (const RegisteredMount& mount : mounts_)
        {
            if (matches_mount(path, mount.descriptor.virtual_root))
            {
                first = &mount;
                break;
            }
        }
        if (first == nullptr)
        {
            return FileResult<std::vector<RoutedLayer>>(
                file_system_error(FileErrorCode::OutsideRoot, "route", path.utf8(), "virtual path is not mounted"));
        }

        std::vector<RoutedLayer> layers;
        bool found_enumerable_layer = false;
        for (const RegisteredMount& mount : mounts_)
        {
            if (mount.descriptor.virtual_root != first->descriptor.virtual_root)
                continue;
            if (enumeration && !mount.descriptor.allow_enumeration)
                continue;
            found_enumerable_layer = found_enumerable_layer || mount.descriptor.allow_enumeration;
            const FileResult<StorePath> routed =
                relative_store_path(path, mount.descriptor.virtual_root, mount.descriptor.store_root);
            if (!routed.succeeded())
            {
                return FileResult<std::vector<RoutedLayer>>(with_virtual_path(routed.status(), path));
            }
            layers.push_back({&mount, routed.value()});
        }
        if (enumeration && !found_enumerable_layer)
        {
            return FileResult<std::vector<RoutedLayer>>(file_system_error(
                FileErrorCode::AccessDenied, "enumerate", path.utf8(), "mount does not allow enumeration"));
        }
        return FileResult<std::vector<RoutedLayer>>(std::move(layers));
    }

    FileResult<FileSystem::RoutedLayer> FileSystem::route_write(const VirtualPath& path) const
    {
        const FileResult<std::vector<RoutedLayer>> layers = route_read(path, false);
        if (!layers.succeeded())
            return FileResult<RoutedLayer>(layers.status());
        for (const RoutedLayer& layer : layers.value())
        {
            if (layer.mount->descriptor.access == MountAccess::ReadWrite)
            {
                return FileResult<RoutedLayer>(layer);
            }
        }
        return FileResult<RoutedLayer>(file_system_error(FileErrorCode::AccessDenied, "route_write", path.utf8(),
                                                         "overlay has no writable layer"));
    }

    FileResult<FileStat> FileSystem::stat(const VirtualPath& path) const
    {
        const FileResult<std::vector<RoutedLayer>> layers = route_read(path, false);
        if (!layers.succeeded())
            return FileResult<FileStat>(layers.status());
        FileStatus not_found =
            file_system_error(FileErrorCode::NotFound, "stat", path.utf8(), "file was not found in any overlay layer");
        for (const RoutedLayer& layer : layers.value())
        {
            const FileResult<FileStat> result = layer.mount->descriptor.store->stat(layer.path);
            if (result.succeeded())
                return result;
            if (result.status().code != FileErrorCode::NotFound)
                return FileResult<FileStat>(with_virtual_path(result.status(), path));
            not_found = with_virtual_path(result.status(), path);
        }
        return FileResult<FileStat>(std::move(not_found));
    }

    FileResult<std::unique_ptr<FileHandle>> FileSystem::open(const VirtualPath& path, FileOpenMode mode) const
    {
        if (mode != FileOpenMode::Read)
        {
            const FileResult<RoutedLayer> layer = route_write(path);
            if (!layer.succeeded())
                return FileResult<std::unique_ptr<FileHandle>>(layer.status());
            FileResult<std::unique_ptr<FileHandle>> opened =
                layer.value().mount->descriptor.store->open(layer.value().path, mode);
            if (!opened.succeeded())
                return FileResult<std::unique_ptr<FileHandle>>(with_virtual_path(opened.status(), path));
            return FileResult<std::unique_ptr<FileHandle>>(std::make_unique<StoreOwnedFileHandle>(
                layer.value().mount->descriptor.store, std::move(opened.value())));
        }

        const FileResult<std::vector<RoutedLayer>> layers = route_read(path, false);
        if (!layers.succeeded())
            return FileResult<std::unique_ptr<FileHandle>>(layers.status());
        FileStatus not_found =
            file_system_error(FileErrorCode::NotFound, "open", path.utf8(), "file was not found in any overlay layer");
        for (const RoutedLayer& layer : layers.value())
        {
            FileResult<std::unique_ptr<FileHandle>> opened = layer.mount->descriptor.store->open(layer.path, mode);
            if (opened.succeeded())
            {
                return FileResult<std::unique_ptr<FileHandle>>(
                    std::make_unique<StoreOwnedFileHandle>(layer.mount->descriptor.store, std::move(opened.value())));
            }
            if (opened.status().code != FileErrorCode::NotFound)
                return FileResult<std::unique_ptr<FileHandle>>(with_virtual_path(opened.status(), path));
            not_found = with_virtual_path(opened.status(), path);
        }
        return FileResult<std::unique_ptr<FileHandle>>(std::move(not_found));
    }

    FileResult<std::vector<std::uint8_t>> FileSystem::read_binary(const VirtualPath& path,
                                                                  std::size_t maximum_size) const
    {
        FileResult<std::unique_ptr<FileHandle>> opened = open(path, FileOpenMode::Read);
        if (!opened.succeeded())
            return FileResult<std::vector<std::uint8_t>>(opened.status());
        std::unique_ptr<FileHandle> handle = std::move(opened.value());
        const FileResult<std::uint64_t> file_size = handle->size();
        if (!file_size.succeeded())
            return FileResult<std::vector<std::uint8_t>>(with_virtual_path(file_size.status(), path));
        if (file_size.value() > maximum_size)
        {
            return FileResult<std::vector<std::uint8_t>>(file_system_error(
                FileErrorCode::TooLarge, "read_binary", path.utf8(), "file exceeds the configured read limit"));
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size.value()));
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const FileResult<std::size_t> read = handle->read(bytes.data() + offset, bytes.size() - offset);
            if (!read.succeeded())
                return FileResult<std::vector<std::uint8_t>>(with_virtual_path(read.status(), path));
            if (read.value() == 0)
                break;
            offset += read.value();
        }
        bytes.resize(offset);
        const FileStatus closed = handle->close();
        if (!closed.succeeded())
            return FileResult<std::vector<std::uint8_t>>(with_virtual_path(closed, path));
        return FileResult<std::vector<std::uint8_t>>(std::move(bytes));
    }

    FileResult<std::string> FileSystem::read_text_utf8(const VirtualPath& path, std::size_t maximum_size) const
    {
        const FileResult<std::vector<std::uint8_t>> bytes = read_binary(path, maximum_size);
        if (!bytes.succeeded())
            return FileResult<std::string>(bytes.status());
        std::string text(bytes.value().begin(), bytes.value().end());
        if (!is_valid_utf8(text))
        {
            return FileResult<std::string>(file_system_error(FileErrorCode::InvalidData, "read_text_utf8", path.utf8(),
                                                             "file is not valid UTF-8"));
        }
        return FileResult<std::string>(std::move(text));
    }

    FileStatus FileSystem::write_binary(const VirtualPath& path, const std::vector<std::uint8_t>& bytes,
                                        FileWriteMode mode)
    {
        const FileOpenMode open_mode =
            mode == FileWriteMode::CreateNew ? FileOpenMode::WriteNew : FileOpenMode::WriteTruncate;
        FileResult<std::unique_ptr<FileHandle>> opened = open(path, open_mode);
        if (!opened.succeeded())
            return opened.status();
        std::unique_ptr<FileHandle> handle = std::move(opened.value());
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const FileResult<std::size_t> written = handle->write(bytes.data() + offset, bytes.size() - offset);
            if (!written.succeeded())
                return with_virtual_path(written.status(), path);
            if (written.value() == 0)
            {
                return file_system_error(FileErrorCode::IoError, "write_binary", path.utf8(), "write made no progress");
            }
            offset += written.value();
        }
        const FileStatus flushed = handle->flush();
        if (!flushed.succeeded())
            return with_virtual_path(flushed, path);
        return with_virtual_path(handle->close(), path);
    }

    FileStatus FileSystem::write_binary_atomic(const VirtualPath& path, const std::vector<std::uint8_t>& bytes,
                                               FilePublishMode mode)
    {
        const FileResult<RoutedLayer> layer = route_write(path);
        if (!layer.succeeded())
            return layer.status();
        if (layer.value().path.empty())
        {
            return file_system_error(FileErrorCode::InvalidPath, "write_binary_atomic", path.utf8(),
                                     "a mount root cannot be replaced by a file");
        }

        static std::atomic<std::uint64_t> next_staging_sequence{1};
        std::unique_ptr<FileHandle> handle;
        StorePath staging;
        constexpr std::size_t maximum_staging_attempts = 64;
        for (std::size_t attempt = 0; attempt < maximum_staging_attempts; ++attempt)
        {
            const FileResult<StorePath> candidate =
                staging_path_for(layer.value().path, next_staging_sequence.fetch_add(1, std::memory_order_relaxed));
            if (!candidate.succeeded())
                return with_virtual_path(candidate.status(), path);
            FileResult<std::unique_ptr<FileHandle>> opened =
                layer.value().mount->descriptor.store->open(candidate.value(), FileOpenMode::WriteNew);
            if (opened.succeeded())
            {
                staging = candidate.value();
                handle = std::move(opened.value());
                break;
            }
            if (opened.status().code != FileErrorCode::AlreadyExists)
                return with_virtual_path(opened.status(), path);
        }
        if (handle == nullptr)
        {
            return file_system_error(FileErrorCode::Busy, "write_binary_atomic", path.utf8(),
                                     "could not allocate a unique staging file");
        }

        auto cleanup_staging = [&]()
        {
            const FileStatus ignored = layer.value().mount->descriptor.store->remove_file(staging);
            (void)ignored;
        };
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const FileResult<std::size_t> written = handle->write(bytes.data() + offset, bytes.size() - offset);
            if (!written.succeeded() || written.value() == 0)
            {
                const FileStatus failure = written.succeeded()
                                               ? file_system_error(FileErrorCode::IoError, "write_binary_atomic",
                                                                   path.utf8(), "staging write made no progress")
                                               : with_virtual_path(written.status(), path);
                const FileStatus ignored_close = handle->close();
                (void)ignored_close;
                cleanup_staging();
                return failure;
            }
            offset += written.value();
        }
        const FileStatus flushed = handle->flush();
        if (!flushed.succeeded())
        {
            const FileStatus ignored_close = handle->close();
            (void)ignored_close;
            cleanup_staging();
            return with_virtual_path(flushed, path);
        }
        const FileStatus closed = handle->close();
        if (!closed.succeeded())
        {
            cleanup_staging();
            return with_virtual_path(closed, path);
        }

        const FileStatus published =
            mode == FilePublishMode::CreateNew
                ? layer.value().mount->descriptor.store->rename_no_replace(staging, layer.value().path)
                : layer.value().mount->descriptor.store->replace(staging, layer.value().path);
        if (!published.succeeded())
        {
            cleanup_staging();
            return with_virtual_path(published, path);
        }
        return FileStatus::success();
    }

    FileResult<std::vector<VirtualDirectoryEntry>> FileSystem::enumerate(const VirtualPath& path) const
    {
        const FileResult<std::vector<RoutedLayer>> layers = route_read(path, true);
        if (!layers.succeeded())
            return FileResult<std::vector<VirtualDirectoryEntry>>(layers.status());
        std::map<std::string, VirtualDirectoryEntry> merged;
        bool any_succeeded = false;
        FileStatus not_found = file_system_error(FileErrorCode::NotFound, "enumerate", path.utf8(),
                                                 "directory was not found in any overlay layer");
        for (const RoutedLayer& layer : layers.value())
        {
            const FileResult<std::vector<StoreDirectoryEntry>> entries =
                layer.mount->descriptor.store->enumerate(layer.path);
            if (!entries.succeeded())
            {
                if (entries.status().code == FileErrorCode::NotFound)
                {
                    not_found = with_virtual_path(entries.status(), path);
                    continue;
                }
                return FileResult<std::vector<VirtualDirectoryEntry>>(with_virtual_path(entries.status(), path));
            }
            any_succeeded = true;
            for (const StoreDirectoryEntry& entry : entries.value())
            {
                merged.emplace(entry.name, VirtualDirectoryEntry{entry.name, entry.type});
            }
        }
        if (!any_succeeded)
            return FileResult<std::vector<VirtualDirectoryEntry>>(std::move(not_found));
        std::vector<VirtualDirectoryEntry> result;
        result.reserve(merged.size());
        for (const auto& entry : merged)
            result.push_back(entry.second);
        return FileResult<std::vector<VirtualDirectoryEntry>>(std::move(result));
    }

    FileStatus FileSystem::create_directories(const VirtualPath& path)
    {
        const FileResult<RoutedLayer> layer = route_write(path);
        if (!layer.succeeded())
            return layer.status();
        return with_virtual_path(layer.value().mount->descriptor.store->create_directories(layer.value().path), path);
    }

    FileStatus FileSystem::remove_file(const VirtualPath& path)
    {
        const FileResult<RoutedLayer> layer = route_write(path);
        if (!layer.succeeded())
            return layer.status();
        return with_virtual_path(layer.value().mount->descriptor.store->remove_file(layer.value().path), path);
    }

    FileStatus FileSystem::remove_empty_directory(const VirtualPath& path)
    {
        const FileResult<RoutedLayer> layer = route_write(path);
        if (!layer.succeeded())
            return layer.status();
        return with_virtual_path(layer.value().mount->descriptor.store->remove_empty_directory(layer.value().path),
                                 path);
    }

    FileStatus FileSystem::rename_no_replace(const VirtualPath& source, const VirtualPath& destination)
    {
        const FileResult<RoutedLayer> source_layer = route_write(source);
        if (!source_layer.succeeded())
            return source_layer.status();
        const FileResult<RoutedLayer> destination_layer = route_write(destination);
        if (!destination_layer.succeeded())
            return destination_layer.status();
        if (source_layer.value().mount->descriptor.store != destination_layer.value().mount->descriptor.store)
        {
            return file_system_error(FileErrorCode::CrossDevice, "rename_no_replace", destination.utf8(),
                                     "cross-store rename is not supported");
        }
        return with_virtual_path(source_layer.value().mount->descriptor.store->rename_no_replace(
                                     source_layer.value().path, destination_layer.value().path),
                                 destination);
    }
} // namespace toy3d
