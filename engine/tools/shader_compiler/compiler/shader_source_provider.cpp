#include "compiler/shader_source_provider.h"

#include <utility>

#include "file_system/virtual_path.h"

namespace toy3d::shader
{
    namespace
    {
        constexpr const char* engine_shader_include_root = "/Engine/ShaderIncludes/";

        bool is_allowed_include_path(const std::string& path)
        {
            const auto parsed = VirtualPath::parse(path);
            return parsed.succeeded() && parsed.value().utf8() == path &&
                parsed.value().utf8().size() >
                std::char_traits<char>::length(engine_shader_include_root) &&
                parsed.value().utf8().compare(
                    0,
                    std::char_traits<char>::length(engine_shader_include_root),
                    engine_shader_include_root) == 0;
        }
    }

    bool ShaderSourceLoadResult::succeeded() const
    {
        return source.has_value() && error.empty();
    }

    RegisteredShaderSourceProvider::RegisteredShaderSourceProvider(
        std::vector<VirtualIncludeFile> files)
    {
        for (VirtualIncludeFile& file : files)
        {
            if (!is_allowed_include_path(file.virtual_path))
            {
                validation_error_ =
                    "Include files must use a normalized /Engine/ShaderIncludes/ virtual path.";
                continue;
            }
            ShaderSourceRecord record;
            record.virtual_path = std::move(file.virtual_path);
            record.source = std::move(file.source);
            record.content_hash = sha256(record.source);
            const std::string key = record.virtual_path;
            if (!files_.emplace(key, std::move(record)).second)
            {
                validation_error_ = "Duplicate virtual include path.";
            }
        }
    }

    ShaderSourceLoadResult RegisteredShaderSourceProvider::load(
        const std::string& virtual_path) const
    {
        ShaderSourceLoadResult result;
        if (!validation_error_.empty())
        {
            result.error = validation_error_;
            return result;
        }
        if (!is_allowed_include_path(virtual_path))
        {
            result.error =
                "Includes must use a normalized /Engine/ShaderIncludes/ virtual path.";
            return result;
        }
        const auto found = files_.find(virtual_path);
        if (found == files_.end())
        {
            result.error = "Virtual include was not provided: " + virtual_path;
            return result;
        }
        result.source = found->second;
        return result;
    }

    const std::string& RegisteredShaderSourceProvider::validation_error() const
    {
        return validation_error_;
    }
}
