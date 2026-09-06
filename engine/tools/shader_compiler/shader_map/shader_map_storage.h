#pragma once

#include "file_system/platform_file.h"

#include <optional>
#include <string>

namespace toy3d::shader
{
    struct ShaderEntryStagingResult
    {
        // Staging and final paths become available at distinct successful phases,
        // so optional represents each publication point independently.
        std::optional<PhysicalPath> staging_directory;
        std::optional<PhysicalPath> final_directory;
        FileStatus status;

        bool succeeded() const;
    };

    ShaderEntryStagingResult create_shader_entry_staging_directory(PlatformFile& platform_file,
                                                                   const PhysicalPath& entry_root,
                                                                   const std::string& key);

    FileStatus publish_shader_entry_directory(PlatformFile& platform_file, const PhysicalPath& staging_directory,
                                              const PhysicalPath& final_directory);

    FileStatus cleanup_shader_entry_staging_directory(PlatformFile& platform_file,
                                                      const PhysicalPath& staging_directory);
} // namespace toy3d::shader
