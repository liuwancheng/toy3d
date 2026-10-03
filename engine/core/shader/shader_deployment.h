#pragma once

#include "shader/shader_build_settings.h"
#include "shader/shader_map_index.h"

namespace toy3d::shader
{
    constexpr std::size_t max_shader_deployment_bytes = 1024u * 1024u;
    struct ShaderDeploymentSource
    {
        std::string name;
        Sha256Hash source_hash{};
        std::vector<Sha256Hash> configurations;
    };
    // Created last in an immutable Cook directory. The exact required source
    // and configuration coverage is checked before runtime publication.
    struct ShaderDeployment
    {
        ShaderCompilePolicy policy;
        std::vector<ShaderDeploymentSource> sources;
        std::size_t required_programs = 0u;
    };
    bool validate_shader_deployment(const ShaderDeployment& deployment, std::string& error);
    std::string serialize_shader_deployment(const ShaderDeployment& deployment);
    bool parse_shader_deployment(const std::string& text, ShaderDeployment& deployment, std::string& error);
    bool read_shader_deployment(const PlatformFile& files, const PhysicalPath& root, ShaderDeployment& deployment,
                                std::string& error);
} // namespace toy3d::shader
