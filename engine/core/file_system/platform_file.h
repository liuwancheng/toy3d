#pragma once

#include "file_system/file_error.h"
#include "file_system/file_handle.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    enum class FileType
    {
        File,
        Directory,
        Symlink,
        Other
    };

    enum class FileWriteMode
    {
        CreateNew,
        Truncate
    };

    struct FileStat
    {
        FileType type = FileType::Other;
        std::uintmax_t size = 0;
    };

    struct DirectoryEntry
    {
        PhysicalPath path;
        FileType type = FileType::Other;
    };

    struct PlatformFileCapabilities
    {
        bool supports_seek = true;
        bool supports_read_at = true;
        bool supports_atomic_rename = true;
        bool supports_atomic_replace = false;
        bool case_sensitive_lookup = false;
    };

    // C++17 inline constexpr gives all FileSystem targets one header-defined
    // read limit without an out-of-line storage definition.
    inline constexpr std::size_t default_maximum_file_read_size =
        static_cast<std::size_t>(512) * 1024 * 1024;

    class PlatformFile
    {
    public:
        virtual ~PlatformFile() = default;

        virtual PlatformFileCapabilities capabilities() const = 0;
        virtual FileResult<std::unique_ptr<FileHandle>> open(
            const PhysicalPath& path,
            FileOpenMode mode) const = 0;
        virtual FileResult<FileStat> stat(const PhysicalPath& path) const = 0;
        virtual FileResult<bool> exists(const PhysicalPath& path) const = 0;
        virtual FileResult<std::vector<std::uint8_t>> read_binary(const PhysicalPath& path) const = 0;
        virtual FileResult<std::string> read_text_utf8(const PhysicalPath& path) const = 0;
        virtual FileStatus write_text_utf8(
            const PhysicalPath& path,
            const std::string& text,
            FileWriteMode mode) = 0;
        virtual FileStatus write_binary(
            const PhysicalPath& path,
            const std::vector<std::uint8_t>& bytes,
            FileWriteMode mode) = 0;
        virtual FileStatus create_directory(const PhysicalPath& path) = 0;
        virtual FileStatus create_directories(const PhysicalPath& path) = 0;
        virtual FileStatus rename_no_replace(
            const PhysicalPath& source,
            const PhysicalPath& destination) = 0;
        virtual FileStatus replace(
            const PhysicalPath& source,
            const PhysicalPath& destination) = 0;
        virtual FileStatus remove_file(const PhysicalPath& path) = 0;
        virtual FileStatus remove_empty_directory(const PhysicalPath& path) = 0;
        virtual FileResult<std::uintmax_t> remove_directory_tree(const PhysicalPath& path) = 0;
        virtual FileResult<std::vector<DirectoryEntry>> enumerate_directory(
            const PhysicalPath& path) const = 0;
        virtual FileResult<PhysicalPath> absolute(const PhysicalPath& path) const = 0;
        virtual FileResult<PhysicalPath> lexically_normal(const PhysicalPath& path) const = 0;
        virtual FileResult<PhysicalPath> canonical(const PhysicalPath& path) const = 0;
        virtual FileResult<PhysicalPath> parent_path(const PhysicalPath& path) const = 0;
        virtual FileResult<PhysicalPath> join_relative(
            const PhysicalPath& base,
            const std::string& generic_relative_path) const = 0;
    };
}
