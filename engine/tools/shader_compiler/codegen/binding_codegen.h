#pragma once

#include "layout/binding_allocator.h"

#include <optional>
#include <string>

namespace toy3d::shader
{
    struct BindingCodegenResult
    {
        std::optional<std::string> source;
        std::vector<Diagnostic> diagnostics;
        Sha256Hash compile_key{};

        bool succeeded() const;
    };

    BindingCodegenResult generate_binding_hlsl(
        const LogicalShaderLayout& logical_layout,
        const TargetBindingLayout& target_layout,
        ShaderStageFlags stage);
}
