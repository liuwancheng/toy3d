#pragma once

#include "compiler/variant_permutation.h"
#include "frontend/diagnostic.h"

namespace toy3d::shader
{
    // Expansion is compiler-owned. The parsed source remains unchanged for
    // revision hashing, discovery and editor diagnostics.
    bool expand_standard_surface(const ShaderAsset& source, const std::vector<ShaderVariantSelection>& selections,
                                 ShaderAsset& expanded, std::vector<Diagnostic>& diagnostics);
    ShaderPass standard_surface_probe(const ShaderAsset& source, bool coverage);
} // namespace toy3d::shader
