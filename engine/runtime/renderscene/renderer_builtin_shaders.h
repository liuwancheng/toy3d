#pragma once

#include <vector>

namespace toy3d
{
    class GlobalShaderType;

    // Explicit composition helper only; it owns no registry or Shader state.
    std::vector<const GlobalShaderType*> required_renderer_global_shader_types(
        bool enable_imgui);
}
