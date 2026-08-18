#pragma once

#include "file_system/platform_file.h"

namespace toy3d
{
    class NativePlatformFile final : public PlatformFile
    {
    public:
        PlatformFileCapabilities capabilities() const override;
        FileResult<std::unique_ptr<FileHandle>> open(
            const PhysicalPath& path,
            FileOpenMode mode) const override;
        FileResult<FileStat> stat(const PhysicalPath& path) const override;
        FileResult<bool> exists(const PhysicalPath& path) const override;
        FileResult<std::vector<std::uint8_t>> read_binary(const PhysicalPath& path) const override;
        FileResult<std::string> read_text_utf8(const PhysicalPath& path) const override;
        FileStatus write_text_utf8(
            const PhysicalPath& path,
            const std::string& text,
            FileWriteMode mode) override;
        FileStatus write_binary(
            const PhysicalPath& path,
            const std::vector<std::uint8_t>& bytes,
            FileWriteMode mode) override;
        FileStatus create_directory(const PhysicalPath& path) override;
        FileStatus create_directories(const PhysicalPath& path) override;
        FileStatus rename_no_replace(
            const PhysicalPath& source,
            const PhysicalPath& destination) override;
        FileStatus replace(
            const PhysicalPath& source,
            const PhysicalPath& destination) override;
        FileStatus remove_file(const PhysicalPath& path) override;
        FileStatus remove_empty_directory(const PhysicalPath& path) override;
        FileResult<std::uintmax_t> remove_directory_tree(const PhysicalPath& path) override;
        FileResult<std::vector<DirectoryEntry>> enumerate_directory(
            const PhysicalPath& path) const override;
        FileResult<PhysicalPath> absolute(const PhysicalPath& path) const override;
        FileResult<PhysicalPath> lexically_normal(const PhysicalPath& path) const override;
        FileResult<PhysicalPath> canonical(const PhysicalPath& path) const override;
        FileResult<PhysicalPath> parent_path(const PhysicalPath& path) const override;
        FileResult<PhysicalPath> join_relative(
            const PhysicalPath& base,
            const std::string& generic_relative_path) const override;
    };
}
