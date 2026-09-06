#pragma once

#include "rendercore/shader/shader_map_program.h"

#include <string>

namespace toy3d
{
    // This is the composition-root boundary between backend configuration and
    // Shader runtime identity. Public RHI and RenderScene must not perform it.
    bool try_get_shader_platform_for_backend(
        const std::string& backend_name,
        ShaderPlatform& output,
        std::string& error);
}
