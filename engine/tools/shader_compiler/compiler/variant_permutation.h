#pragma once

#include "shader/shader_permutation.h"
#include "shader/shader_compile_plan.h"
#include "frontend/diagnostic.h"
#include "frontend/shader_ast.h"

#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    struct ShaderVariantSelection
    {
        std::string name;
        std::string value;
    };

    struct ShaderPermutationResult
    {
        // optional prevents invalid option selections from producing a partial permutation.
        std::optional<ShaderPermutation> permutation;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderPermutationResult resolve_shader_permutation(const ShaderAsset& asset,
                                                       const std::vector<ShaderVariantSelection>& selections);
    ShaderPermutationDomain shader_material_domain(const ShaderAsset& asset);
    ShaderCompileSource shader_compile_source(const ShaderAsset& asset);
} // namespace toy3d::shader
