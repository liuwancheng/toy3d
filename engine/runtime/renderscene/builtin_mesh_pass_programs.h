#pragma once

#include <memory>

namespace toy3d
{
    class ShaderMapProgram;

    // Renderer-owned references to built-in mesh pass programs loaded before
    // the Rendering Thread starts. Material-specific programs stay with materials.
    struct BuiltinMeshPassPrograms
    {
        std::shared_ptr<const ShaderMapProgram> shadow_depth_default;
    };
} // namespace toy3d
