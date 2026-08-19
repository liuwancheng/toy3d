#pragma once

#include "file_system/file_store.h"

#include <memory>

namespace toy3d
{
    enum class DirectorySymlinkPolicy
    {
        Deny,
        AllowWithinRoot
    };

    struct DirectoryFileStoreDesc
    {
        PhysicalPath physical_root;
        bool writable = true;
        DirectorySymlinkPolicy symlink_policy = DirectorySymlinkPolicy::Deny;
        std::string debug_name;
    };

    class DirectoryFileStore final : public FileStore
    {
    public:
        static FileResult<std::shared_ptr<DirectoryFileStore>> create(
            PlatformFile& platform_file,
            const DirectoryFileStoreDesc& descriptor);

        DirectoryFileStore(const DirectoryFileStore&) = delete;
        DirectoryFileStore& operator=(const DirectoryFileStore&) = delete;
        // The factory moves its fully validated value into make_shared; the
        // descriptor constructor remains private so callers cannot bypass validation.
        DirectoryFileStore(DirectoryFileStore&&) noexcept = default;
        DirectoryFileStore& operator=(DirectoryFileStore&&) = delete;

        FileStoreCapabilities capabilities() const override;
        FileResult<FileStat> stat(const StorePath& path) const override;
        FileResult<std::unique_ptr<FileHandle>> open(
            const StorePath& path,
            FileOpenMode mode) override;
        FileResult<std::vector<StoreDirectoryEntry>> enumerate(
            const StorePath& path) const override;
        FileStatus create_directories(const StorePath& path) override;
        FileStatus remove_file(const StorePath& path) override;
        FileStatus remove_empty_directory(const StorePath& path) override;
        FileStatus rename_no_replace(
            const StorePath& source,
            const StorePath& destination) override;
        FileStatus replace(
            const StorePath& source,
            const StorePath& destination) override;

        // Migration-only adapter. New callers must not obtain physical paths.
        FileResult<PhysicalPath> resolve_physical_for_adapter(const StorePath& path) const;

    private:
        DirectoryFileStore(
            PlatformFile& platform_file,
            DirectoryFileStoreDesc descriptor,
            PhysicalPath canonical_root);

        FileResult<PhysicalPath> resolve_physical(const StorePath& path) const;
        FileStatus ensure_writable(const char* operation, const StorePath& path) const;

        PlatformFile& platform_file_;
        DirectoryFileStoreDesc descriptor_;
        PhysicalPath canonical_root_;
    };
}
