#pragma once

#include "frontend/diagnostic.h"
#include "frontend/shader_ast.h"
#include "layout/shader_layout.h"

#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    struct ShaderParametersCodegenResult
    {
        std::string output_name;
        // optional withholds the complete translation unit whenever schema or
        // identifier validation fails, preventing partial generated headers.
        std::optional<std::string> source;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderParametersCodegenResult generate_shader_parameters_header(const ShaderAsset& asset,
                                                                     const LogicalShaderLayout& layout);
    ShaderParametersCodegenResult generate_builtin_shader_parameters_header();
} // namespace toy3d::shader
