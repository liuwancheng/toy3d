#include "compiler/variant_permutation.h"

#include <algorithm>
#include <utility>

namespace toy3d::shader
{
    bool ShaderPermutationResult::succeeded() const
    {
        return permutation.has_value() && diagnostics.empty();
    }

    ShaderPermutationDomain shader_material_domain(const ShaderAsset& asset)
    {
        // AST locations belong to Tools. Core owns the domain validation,
        // normalization, stable identity and macro generation for all consumers.
        ShaderPermutationDomain domain;
        for (const Variant& variant : asset.variants)
        {
            ShaderPermutationDimension dimension;
            dimension.name = variant.name;
            dimension.affected_stages = variant.affected_stages;
            dimension.affected_passes = variant.affected_passes;
            if (variant.type == VariantType::Boolean)
            {
                dimension.kind = ShaderPermutationValueKind::Boolean;
                dimension.boolean_default = variant.default_value == "true";
                // Reject inconsistent ASTs even when the parser was bypassed.
                dimension.options = variant.options;
            }
            else if (variant.type == VariantType::Enumeration)
            {
                dimension.kind = ShaderPermutationValueKind::Enumeration;
                dimension.options = variant.options;
                dimension.enum_default = variant.default_value;
            }
            else
            {
                dimension.kind = static_cast<ShaderPermutationValueKind>(~0u);
            }
            domain.dimensions.push_back(std::move(dimension));
        }
        return domain;
    }

    ShaderCompileSource shader_compile_source(const ShaderAsset& asset)
    {
        ShaderCompileSource source;
        source.name = asset.name;
        source.usage = asset.usage;
        source.geometry = asset.geometry;
        source.vertex_factory_support = asset.vertex_factory_support;
        source.material_domain = shader_material_domain(asset);
        source.features = asset.features;
        source.supported_when = asset.supported_when;
        source.standard_tangent_input = asset.standard_tangent_input;
        source.declares_tangent_frame = asset.declares_tangent_frame;
        source.tangent_frame_when = asset.tangent_frame_when;
        for (const auto& pass : asset.passes)
        {
            source.passes.push_back({pass.name, pass.role});
        }
        return source;
    }

    ShaderPermutationResult resolve_shader_permutation(const ShaderAsset& asset,
                                                       const std::vector<ShaderVariantSelection>& selections)
    {
        const auto domain = shader_material_domain(asset);
        ShaderPermutationResult result;
        for (const auto& variant : asset.variants)
        {
            if (variant.type == VariantType::Boolean && variant.default_value != "true" &&
                variant.default_value != "false")
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidVariant,
                                              variant.location, "Boolean default must be true or false."});
            }
        }

        std::vector<ShaderPermutationSelection> typed_selections;
        for (const ShaderVariantSelection& selection : selections)
        {
            ShaderPermutationSelection typed;
            typed.name = selection.name;
            const auto declaration = std::find_if(asset.variants.begin(), asset.variants.end(),
                                                  [&](const Variant& variant)
                                                  {
                                                      return variant.name == selection.name;
                                                  });
            if (declaration != asset.variants.end() && declaration->type == VariantType::Enumeration)
            {
                typed.kind = ShaderPermutationValueKind::Enumeration;
                typed.enum_value = selection.value;
            }
            else
            {
                typed.boolean_value = selection.value == "true";
                if (selection.value != "true" && selection.value != "false")
                {
                    result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidVariantSelection,
                                                  asset.location, "Boolean Variant selection requires true or false."});
                }
            }
            typed_selections.push_back(std::move(typed));
        }
        if (!result.diagnostics.empty())
        {
            return result;
        }
        ShaderPermutationResolution resolved = resolve_shader_permutation(domain, typed_selections);
        for (const ShaderPermutationError& error : resolved.errors)
        {
            const auto declaration = std::find_if(asset.variants.begin(), asset.variants.end(),
                                                  [&](const Variant& variant)
                                                  {
                                                      return variant.name == error.dimension_name;
                                                  });
            DiagnosticCode code = DiagnosticCode::InvalidVariant;
            if (error.code == ShaderPermutationErrorCode::InvalidSelection)
            {
                code = DiagnosticCode::InvalidVariantSelection;
            }
            else if (error.code == ShaderPermutationErrorCode::IdentityCollision)
            {
                code = DiagnosticCode::VariantIdCollision;
            }
            result.diagnostics.push_back({DiagnosticSeverity::Error, code,
                                          declaration == asset.variants.end() ? asset.location : declaration->location,
                                          error.message});
        }
        if (resolved.succeeded())
        {
            result.permutation = std::move(resolved.permutation);
        }
        return result;
    }
} // namespace toy3d::shader
