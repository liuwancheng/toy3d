#include "file_system/directory_file_store.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "platform/platform_defines.h"

namespace toy3d
{
    namespace
    {
        std::string comparable_physical_path(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            while (path.size() > 1 && path.back() == '/' && !(path.size() == 3 && path[1] == ':'))
            {
                path.pop_back();
            }
#if WITH_WIN
            std::transform(path.begin(), path.end(), path.begin(),
                           [](unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
#endif
            return path;
        }

        bool is_within_root(const PhysicalPath& path, const PhysicalPath& root)
        {
            const std::string candidate = comparable_physical_path(path.utf8());
            const std::string boundary = comparable_physical_path(root.utf8());
            if (candidate == boundary)
            {
                return true;
            }
            if (!boundary.empty() && boundary.back() == '/')
            {
                return candidate.size() > boundary.size() && candidate.compare(0, boundary.size(), boundary) == 0;
            }
            return candidate.size() > boundary.size() && candidate.compare(0, boundary.size(), boundary) == 0 &&
                   candidate[boundary.size()] == '/';
        }

        FileStatus store_error(FileErrorCode code, const char* operation, const StorePath& path, std::string message)
        {
            FileStatus status;
            status.code = code;
            status.operation = operation;
            status.virtual_path = path.utf8();
            status.message = std::move(message);
            return status;
        }

        FileStatus with_store_path(FileStatus status, const StorePath& path)
        {
            if (status.virtual_path.empty())
            {
                status.virtual_path = path.utf8();
            }
            return status;
        }

        std::vector<std::string> split_segments(const StorePath& path)
        {
            std::vector<std::string> segments;
            std::size_t start = 0;
            while (start < path.utf8().size())
            {
                const std::size_t separator = path.utf8().find('/', start);
                const std::size_t end = separator == std::string::npos ? path.utf8().size() : separator;
                segments.push_back(path.utf8().substr(start, end - start));
                if (separator == std::string::npos)
                {
                    break;
                }
                start = separator + 1;
            }
            return segments;
        }

        std::string entry_name(const PhysicalPath& path)
        {
            std::string value = path.utf8();
            std::replace(value.begin(), value.end(), '\\', '/');
            const std::size_t separator = value.find_last_of('/');
            return separator == std::string::npos ? value : value.substr(separator + 1);
        }
    } // namespace

    DirectoryFileStore::DirectoryFileStore(PlatformFile& platform_file, DirectoryFileStoreDesc descriptor,
                                           PhysicalPath canonical_root)
        : platform_file_(platform_file), descriptor_(std::move(descriptor)), canonical_root_(std::move(canonical_root))
    {
    }

    FileResult<std::shared_ptr<DirectoryFileStore>> DirectoryFileStore::create(PlatformFile& platform_file,
                                                                               const DirectoryFileStoreDesc& descriptor)
    {
        if (descriptor.physical_root.empty() || !descriptor.physical_root.valid())
        {
            return FileResult<std::shared_ptr<DirectoryFileStore>>(store_error(
                FileErrorCode::InvalidPath, "create_directory_store", StorePath(), "directory store root is invalid"));
        }
        const FileResult<FileStat> root_stat = platform_file.stat(descriptor.physical_root);
        if (!root_stat.succeeded())
        {
            return FileResult<std::shared_ptr<DirectoryFileStore>>(root_stat.status());
        }
        if (root_stat.value().type == FileType::Symlink && descriptor.symlink_policy == DirectorySymlinkPolicy::Deny)
        {
            return FileResult<std::shared_ptr<DirectoryFileStore>>(
                store_error(FileErrorCode::OutsideRoot, "create_directory_store", StorePath(),
                            "symlink or reparse-point store root is forbidden by policy"));
        }
        if (root_stat.value().type != FileType::Directory && root_stat.value().type != FileType::Symlink)
        {
            return FileResult<std::shared_ptr<DirectoryFileStore>>(
                store_error(FileErrorCode::NotDirectory, "create_directory_store", StorePath(),
                            "directory store root must be a directory"));
        }
        const FileResult<PhysicalPath> canonical_root = platform_file.canonical(descriptor.physical_root);
        if (!canonical_root.succeeded())
        {
            return FileResult<std::shared_ptr<DirectoryFileStore>>(canonical_root.status());
        }
        const FileResult<FileStat> canonical_stat = platform_file.stat(canonical_root.value());
        if (!canonical_stat.succeeded())
        {
            return FileResult<std::shared_ptr<DirectoryFileStore>>(canonical_stat.status());
        }
        if (canonical_stat.value().type != FileType::Directory)
        {
            return FileResult<std::shared_ptr<DirectoryFileStore>>(
                store_error(FileErrorCode::NotDirectory, "create_directory_store", StorePath(),
                            "canonical store root must be a directory"));
        }
        DirectoryFileStore store(platform_file, descriptor, canonical_root.value());
        return FileResult<std::shared_ptr<DirectoryFileStore>>(std::make_shared<DirectoryFileStore>(std::move(store)));
    }

    FileStoreCapabilities DirectoryFileStore::capabilities() const
    {
        const PlatformFileCapabilities platform = platform_file_.capabilities();
        FileStoreCapabilities result;
        result.writable = descriptor_.writable;
        result.enumerable = true;
        result.supports_seek = platform.supports_seek;
        result.supports_read_at = platform.supports_read_at;
        result.supports_atomic_rename = platform.supports_atomic_rename;
        result.supports_atomic_replace = platform.supports_atomic_replace;
        return result;
    }

    FileResult<PhysicalPath> DirectoryFileStore::resolve_physical(const StorePath& path) const
    {
        PhysicalPath current = canonical_root_;
        const std::vector<std::string> segments = split_segments(path);
        for (std::size_t index = 0; index < segments.size(); ++index)
        {
            const FileResult<PhysicalPath> joined = platform_file_.join_relative(current, segments[index]);
            if (!joined.succeeded())
            {
                return FileResult<PhysicalPath>(with_store_path(joined.status(), path));
            }
            const FileResult<FileStat> child_stat = platform_file_.stat(joined.value());
            if (!child_stat.succeeded())
            {
                if (child_stat.status().code != FileErrorCode::NotFound)
                {
                    return FileResult<PhysicalPath>(with_store_path(child_stat.status(), path));
                }
                current = joined.value();
                for (++index; index < segments.size(); ++index)
                {
                    const FileResult<PhysicalPath> missing_child =
                        platform_file_.join_relative(current, segments[index]);
                    if (!missing_child.succeeded())
                    {
                        return FileResult<PhysicalPath>(with_store_path(missing_child.status(), path));
                    }
                    current = missing_child.value();
                }
                break;
            }
            if (child_stat.value().type == FileType::Symlink &&
                descriptor_.symlink_policy == DirectorySymlinkPolicy::Deny)
            {
                return FileResult<PhysicalPath>(
                    store_error(FileErrorCode::OutsideRoot, "resolve_store_path", path,
                                "symlink or reparse-point traversal is forbidden by store policy"));
            }
            const FileResult<PhysicalPath> canonical_child = platform_file_.canonical(joined.value());
            if (!canonical_child.succeeded())
            {
                return FileResult<PhysicalPath>(with_store_path(canonical_child.status(), path));
            }
            if (!is_within_root(canonical_child.value(), canonical_root_))
            {
                return FileResult<PhysicalPath>(store_error(FileErrorCode::OutsideRoot, "resolve_store_path", path,
                                                            "resolved physical path escapes the store root"));
            }
            if (descriptor_.symlink_policy == DirectorySymlinkPolicy::Deny &&
                comparable_physical_path(joined.value().utf8()) !=
                    comparable_physical_path(canonical_child.value().utf8()))
            {
                return FileResult<PhysicalPath>(store_error(FileErrorCode::OutsideRoot, "resolve_store_path", path,
                                                            "reparse-point traversal is forbidden by store policy"));
            }
            current = canonical_child.value();
        }
        if (!is_within_root(current, canonical_root_))
        {
            return FileResult<PhysicalPath>(store_error(FileErrorCode::OutsideRoot, "resolve_store_path", path,
                                                        "resolved physical path escapes the store root"));
        }
        return FileResult<PhysicalPath>(std::move(current));
    }

    FileResult<PhysicalPath> DirectoryFileStore::resolve_physical_for_adapter(const StorePath& path) const
    {
        return resolve_physical(path);
    }

    FileStatus DirectoryFileStore::ensure_writable(const char* operation, const StorePath& path) const
    {
        if (descriptor_.writable)
        {
            return FileStatus::success();
        }
        return store_error(FileErrorCode::ReadOnly, operation, path, "directory store is read-only");
    }

    FileResult<FileStat> DirectoryFileStore::stat(const StorePath& path) const
    {
        const FileResult<PhysicalPath> resolved = resolve_physical(path);
        if (!resolved.succeeded())
        {
            return FileResult<FileStat>(resolved.status());
        }
        const FileResult<FileStat> result = platform_file_.stat(resolved.value());
        return result.succeeded() ? result : FileResult<FileStat>(with_store_path(result.status(), path));
    }

    FileResult<std::unique_ptr<FileHandle>> DirectoryFileStore::open(const StorePath& path, FileOpenMode mode)
    {
        if (mode != FileOpenMode::Read)
        {
            const FileStatus writable = ensure_writable("open", path);
            if (!writable.succeeded())
            {
                return FileResult<std::unique_ptr<FileHandle>>(writable);
            }
        }
        const FileResult<PhysicalPath> resolved = resolve_physical(path);
        if (!resolved.succeeded())
        {
            return FileResult<std::unique_ptr<FileHandle>>(resolved.status());
        }
        FileResult<std::unique_ptr<FileHandle>> result = platform_file_.open(resolved.value(), mode);
        return result.succeeded() ? std::move(result)
                                  : FileResult<std::unique_ptr<FileHandle>>(with_store_path(result.status(), path));
    }

    FileResult<std::vector<StoreDirectoryEntry>> DirectoryFileStore::enumerate(const StorePath& path) const
    {
        const FileResult<PhysicalPath> resolved = resolve_physical(path);
        if (!resolved.succeeded())
        {
            return FileResult<std::vector<StoreDirectoryEntry>>(resolved.status());
        }
        const FileResult<std::vector<DirectoryEntry>> entries = platform_file_.enumerate_directory(resolved.value());
        if (!entries.succeeded())
        {
            return FileResult<std::vector<StoreDirectoryEntry>>(with_store_path(entries.status(), path));
        }
        std::vector<StoreDirectoryEntry> result;
        result.reserve(entries.value().size());
        for (const DirectoryEntry& entry : entries.value())
        {
            const std::string name = entry_name(entry.path);
            const FileResult<StorePath> validated = StorePath::parse(name);
            if (!validated.succeeded() || validated.value().empty() || name.find('/') != std::string::npos)
            {
                return FileResult<std::vector<StoreDirectoryEntry>>(
                    store_error(FileErrorCode::InvalidData, "enumerate", path,
                                "platform returned an invalid directory entry name"));
            }
            result.push_back({name, entry.type});
        }
        std::sort(result.begin(), result.end(),
                  [](const StoreDirectoryEntry& lhs, const StoreDirectoryEntry& rhs)
                  {
                      return lhs.name < rhs.name;
                  });
        return FileResult<std::vector<StoreDirectoryEntry>>(std::move(result));
    }

    FileStatus DirectoryFileStore::create_directories(const StorePath& path)
    {
        const FileStatus writable = ensure_writable("create_directories", path);
        if (!writable.succeeded())
        {
            return writable;
        }
        const FileResult<PhysicalPath> resolved = resolve_physical(path);
        if (!resolved.succeeded())
        {
            return resolved.status();
        }
        return with_store_path(platform_file_.create_directories(resolved.value()), path);
    }

    FileStatus DirectoryFileStore::remove_file(const StorePath& path)
    {
        const FileStatus writable = ensure_writable("remove_file", path);
        if (!writable.succeeded())
        {
            return writable;
        }
        const FileResult<PhysicalPath> resolved = resolve_physical(path);
        if (!resolved.succeeded())
        {
            return resolved.status();
        }
        return with_store_path(platform_file_.remove_file(resolved.value()), path);
    }

    FileStatus DirectoryFileStore::remove_empty_directory(const StorePath& path)
    {
        const FileStatus writable = ensure_writable("remove_empty_directory", path);
        if (!writable.succeeded())
        {
            return writable;
        }
        if (path.empty())
        {
            return store_error(FileErrorCode::AccessDenied, "remove_empty_directory", path,
                               "the directory store root cannot be removed");
        }
        const FileResult<PhysicalPath> resolved = resolve_physical(path);
        if (!resolved.succeeded())
        {
            return resolved.status();
        }
        return with_store_path(platform_file_.remove_empty_directory(resolved.value()), path);
    }

    FileStatus DirectoryFileStore::rename_no_replace(const StorePath& source, const StorePath& destination)
    {
        const FileStatus writable = ensure_writable("rename_no_replace", destination);
        if (!writable.succeeded())
        {
            return writable;
        }
        const FileResult<PhysicalPath> resolved_source = resolve_physical(source);
        if (!resolved_source.succeeded())
        {
            return resolved_source.status();
        }
        const FileResult<PhysicalPath> resolved_destination = resolve_physical(destination);
        if (!resolved_destination.succeeded())
        {
            return resolved_destination.status();
        }
        return with_store_path(platform_file_.rename_no_replace(resolved_source.value(), resolved_destination.value()),
                               destination);
    }

    FileStatus DirectoryFileStore::replace(const StorePath& source, const StorePath& destination)
    {
        const FileStatus writable = ensure_writable("replace", destination);
        if (!writable.succeeded())
        {
            return writable;
        }
        const FileResult<PhysicalPath> resolved_source = resolve_physical(source);
        if (!resolved_source.succeeded())
        {
            return resolved_source.status();
        }
        const FileResult<PhysicalPath> resolved_destination = resolve_physical(destination);
        if (!resolved_destination.succeeded())
        {
            return resolved_destination.status();
        }
        return with_store_path(platform_file_.replace(resolved_source.value(), resolved_destination.value()),
                               destination);
    }
} // namespace toy3d
