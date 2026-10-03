#pragma once

#include "shader/shader_compile_plan.h"
#include "file_system/platform_file.h"

#include <map>

namespace toy3d::shader
{
    constexpr std::size_t max_shader_build_settings_bytes = 1024u * 1024u;
    constexpr std::size_t max_shader_build_sources = 256u;
    constexpr std::size_t max_shader_build_policies = 6u;
    struct ShaderBuildSettings
    {
        std::vector<ShaderCompilePolicy> policies;
        std::map<std::string, std::vector<std::vector<ShaderPermutationSelection>>> additional_configurations;
    };
    ShaderBuildSettings default_shader_build_settings();
    bool validate_shader_build_settings(const ShaderBuildSettings& settings, std::string& error);
    std::string serialize_shader_build_settings(const ShaderBuildSettings& settings);
    bool parse_shader_build_settings(const std::string& text, ShaderBuildSettings& settings, std::string& error);
    // Engine file is required when specified; Project file is an optional overlay.
    // Explicit profile policies and source extras replace matching engine entries.
    bool read_shader_build_settings(const PlatformFile& files, const PhysicalPath& engine_file,
                                    const PhysicalPath& project_file, ShaderBuildSettings& settings,
                                    std::string& error);
    bool make_shader_source_compile_request(const ShaderBuildSettings& settings, const std::string& shader_name,
                                            ShaderTarget target, ShaderCompileProfile profile, bool editor,
                                            std::vector<std::vector<ShaderPermutationSelection>> configurations,
                                            ShaderSourceCompileRequest& request, std::string& error);
} // namespace toy3d::shader
