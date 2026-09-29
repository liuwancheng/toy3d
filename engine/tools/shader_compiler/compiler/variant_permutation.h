#pragma once

#include "hash/sha256.h"
#include "frontend/diagnostic.h"
#include "frontend/shader_ast.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toy3d::shader
{
    struct ShaderVariantSelection
    {
        std::string name;
        std::string value;
    };

    enum class ShaderPermutationValueKind : std::uint32_t
    {
        Boolean,
        Enumeration
    };

    struct ShaderPermutationRecord
    {
        ShaderVariantId variant_id = 0;
        ShaderPermutationValueKind kind = ShaderPermutationValueKind::Boolean;
        bool boolean_value = false;
        ShaderEnumValueId enum_value_id = 0;
    };

    struct ShaderPermutation
    {
        std::uint32_t variant_id_version = shader_variant_id_version;
        std::uint32_t version = shader_permutation_version;
        std::vector<ShaderPermutationRecord> records;
        Sha256Hash key{};
        std::string generated_prelude;
    };

    struct ShaderPermutationResult
    {
        // optional prevents invalid option selections from producing a partial
        // permutation; string_view below hashes names without copying them.
        std::optional<ShaderPermutation> permutation;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderVariantId make_shader_variant_id(std::string_view name);
    ShaderEnumValueId make_shader_enum_value_id(ShaderVariantId variant_id, std::string_view option_name);

    ShaderPermutationResult resolve_shader_permutation(const ShaderAsset& asset,
                                                       const std::vector<ShaderVariantSelection>& selections);
} // namespace toy3d::shader
