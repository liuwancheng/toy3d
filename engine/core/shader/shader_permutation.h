#pragma once

#include "shader/shader_format_types.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toy3d::shader
{
    constexpr std::size_t max_shader_permutation_dimensions = 32u;
    constexpr std::size_t max_shader_permutation_enum_values = 32u;

    enum class ShaderPermutationScope
    {
        Material,
        Pass
    };

    enum class ShaderPermutationValueKind : std::uint32_t
    {
        Boolean,
        Enumeration
    };

    // Persist the explicit kind with the value so an enum rename or kind change
    // cannot be silently interpreted as another configuration.
    struct ShaderPermutationSelection
    {
        std::string name;
        ShaderPermutationValueKind kind = ShaderPermutationValueKind::Boolean;
        bool boolean_value = false;
        std::string enum_value;
    };

    struct ShaderPermutationDimension
    {
        std::string name;
        ShaderPermutationValueKind kind = ShaderPermutationValueKind::Boolean;
        std::vector<std::string> options;
        bool boolean_default = false;
        std::string enum_default;
        ShaderStageFlags affected_stages =
            ShaderStageFlags::Vertex | ShaderStageFlags::Pixel | ShaderStageFlags::Compute;
    };

    struct ShaderPermutationDomain
    {
        ShaderPermutationScope scope = ShaderPermutationScope::Material;
        std::vector<ShaderPermutationDimension> dimensions;
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

    enum class ShaderPermutationErrorCode
    {
        InvalidDomain,
        InvalidSelection,
        IdentityCollision
    };

    struct ShaderPermutationError
    {
        ShaderPermutationErrorCode code = ShaderPermutationErrorCode::InvalidDomain;
        std::string dimension_name;
        std::string message;
    };

    struct ShaderPermutationResolution
    {
        // Only a fully validated domain and selection may publish a value.
        std::optional<ShaderPermutation> permutation;
        std::vector<ShaderPermutationError> errors;

        bool succeeded() const;
    };

    // string_view observes caller-owned names when deriving stable identities.
    ShaderVariantId make_shader_variant_id(std::string_view name,
                                           ShaderPermutationScope scope = ShaderPermutationScope::Material);
    ShaderEnumValueId make_shader_enum_value_id(ShaderVariantId variant_id, std::string_view option_name);

    // None resolves the full configuration. A single stage projects the validated
    // configuration onto its declared inputs; even omitted dimensions are checked.
    ShaderPermutationResolution resolve_shader_permutation(const ShaderPermutationDomain& domain,
                                                           const std::vector<ShaderPermutationSelection>& selections,
                                                           ShaderStageFlags stage = ShaderStageFlags::None);
} // namespace toy3d::shader
