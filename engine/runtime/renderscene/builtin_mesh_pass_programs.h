#pragma once

#include <memory>

namespace toy3d
{
    class ShaderMapCollection;

    // Renderer-owned references to built-in mesh pass programs loaded before
    // the Rendering Thread starts. Material-specific programs stay with materials.
    struct BuiltinMeshPassPrograms
    {
        std::shared_ptr<const ShaderMapCollection> shadow_depth_default;
        std::shared_ptr<const ShaderMapCollection> hit_proxy;
    };
} // namespace toy3d
