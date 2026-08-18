#pragma once

#include "file_system/file_store.h"
#include "file_system/virtual_path.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    inline constexpr std::uint32_t file_mount_desc_version = 1;

    enum class MountAccess
    {
        ReadOnly,
        ReadWrite
    };

    enum class FilePublishMode
    {
        CreateNew,
        Replace
    };

    struct FileMountDesc
    {
        std::uint32_t version = file_mount_desc_version;
        VirtualPath virtual_root;
        std::shared_ptr<FileStore> store;
        StorePath store_root;
        MountAccess access = MountAccess::ReadOnly;
        bool allow_enumeration = false;
        int priority = 0;
        std::string debug_name;
    };

    struct VirtualDirectoryEntry
    {
        std::string name;
        FileType type = FileType::Other;
    };

    class FileSystem
    {
    public:
        FileStatus add_mount(const FileMountDesc& descriptor);
        FileStatus freeze();
        bool frozen() const;

        FileResult<FileStat> stat(const VirtualPath& path) const;
        FileResult<std::unique_ptr<FileHandle>> open(
            const VirtualPath& path,
            FileOpenMode mode) const;
        FileResult<std::vector<std::uint8_t>> read_binary(
            const VirtualPath& path,
            std::size_t maximum_size = default_maximum_file_read_size) const;
        FileResult<std::string> read_text_utf8(
            const VirtualPath& path,
            std::size_t maximum_size = default_maximum_file_read_size) const;
        FileStatus write_binary(
            const VirtualPath& path,
            const std::vector<std::uint8_t>& bytes,
            FileWriteMode mode);
        FileStatus write_binary_atomic(
            const VirtualPath& path,
            const std::vector<std::uint8_t>& bytes,
            FilePublishMode mode);
        FileResult<std::vector<VirtualDirectoryEntry>> enumerate(
            const VirtualPath& path) const;
        FileStatus create_directories(const VirtualPath& path);
        FileStatus remove_file(const VirtualPath& path);
        FileStatus remove_empty_directory(const VirtualPath& path);
        FileStatus rename_no_replace(
            const VirtualPath& source,
            const VirtualPath& destination);

    private:
        struct RegisteredMount
        {
            FileMountDesc descriptor;
        };

        struct RoutedLayer
        {
            const RegisteredMount* mount = nullptr;
            StorePath path;
        };

        FileResult<std::vector<RoutedLayer>> route_read(
            const VirtualPath& path,
            bool enumeration) const;
        FileResult<RoutedLayer> route_write(const VirtualPath& path) const;

        std::vector<RegisteredMount> mounts_;
        bool frozen_ = false;
    };
}
