#include "shader_map/shader_map_storage.h"

namespace toy3d::shader
{
    namespace
    {
        constexpr std::uint32_t maximum_staging_attempts = 256;
    }

    bool ShaderEntryStagingResult::succeeded() const
    {
        return staging_directory.has_value() && final_directory.has_value() && status.succeeded();
    }

    ShaderEntryStagingResult create_shader_entry_staging_directory(PlatformFile& platform_file,
                                                                   const PhysicalPath& entry_root,
                                                                   const std::string& key)
    {
        ShaderEntryStagingResult result;
        result.status = platform_file.create_directories(entry_root);
        if (!result.status.succeeded())
            return result;

        const FileResult<PhysicalPath> final_directory = platform_file.join_relative(entry_root, key);
        if (!final_directory.succeeded())
        {
            result.status = final_directory.status();
            return result;
        }
        result.final_directory = final_directory.value();

        const FileResult<bool> final_exists = platform_file.exists(*result.final_directory);
        if (!final_exists.succeeded())
        {
            result.status = final_exists.status();
            return result;
        }
        if (final_exists.value())
        {
            result.status.code = FileErrorCode::AlreadyExists;
            result.status.operation = "create_shader_entry_staging_directory";
            result.status.path = *result.final_directory;
            result.status.message = "Shader entry key already exists";
            return result;
        }

        for (std::uint32_t attempt = 0; attempt < maximum_staging_attempts; ++attempt)
        {
            const FileResult<PhysicalPath> candidate =
                platform_file.join_relative(entry_root, key + ".tmp." + std::to_string(attempt));
            if (!candidate.succeeded())
            {
                result.status = candidate.status();
                return result;
            }
            result.status = platform_file.create_directory(candidate.value());
            if (result.status.succeeded())
            {
                result.staging_directory = candidate.value();
                return result;
            }
            if (result.status.code != FileErrorCode::AlreadyExists)
                return result;
        }

        result.status.code = FileErrorCode::AlreadyExists;
        result.status.operation = "create_shader_entry_staging_directory";
        result.status.path = entry_root;
        result.status.message = "No unique Shader entry staging directory is available";
        return result;
    }

    FileStatus publish_shader_entry_directory(PlatformFile& platform_file, const PhysicalPath& staging_directory,
                                              const PhysicalPath& final_directory)
    {
        return platform_file.rename_no_replace(staging_directory, final_directory);
    }

    FileStatus cleanup_shader_entry_staging_directory(PlatformFile& platform_file,
                                                      const PhysicalPath& staging_directory)
    {
        const FileResult<std::uintmax_t> removed = platform_file.remove_directory_tree(staging_directory);
        return removed.succeeded() ? FileStatus::success() : removed.status();
    }
} // namespace toy3d::shader
