#pragma once

#include "file_system/file_handle.h"
#include "file_system/platform_file.h"
#include "file_system/store_path.h"

#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    struct FileStoreCapabilities
    {
        bool writable = false;
        bool enumerable = false;
        bool supports_seek = false;
        bool supports_read_at = false;
        bool supports_atomic_rename = false;
        bool supports_atomic_replace = false;
    };

    struct StoreDirectoryEntry
    {
        std::string name;
        FileType type = FileType::Other;
    };

    class FileStore
    {
      public:
        virtual ~FileStore() = default;

        virtual FileStoreCapabilities capabilities() const = 0;
        virtual FileResult<FileStat> stat(const StorePath& path) const = 0;
        virtual FileResult<std::unique_ptr<FileHandle>> open(const StorePath& path, FileOpenMode mode) = 0;
        virtual FileResult<std::vector<StoreDirectoryEntry>> enumerate(const StorePath& path) const = 0;
        virtual FileStatus create_directories(const StorePath& path) = 0;
        virtual FileStatus remove_file(const StorePath& path) = 0;
        virtual FileStatus remove_empty_directory(const StorePath& path) = 0;
        virtual FileStatus rename_no_replace(const StorePath& source, const StorePath& destination) = 0;
        virtual FileStatus replace(const StorePath& source, const StorePath& destination) = 0;
    };
} // namespace toy3d
