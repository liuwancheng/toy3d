#pragma once

#include "shader/shader_permutation.h"

#include <string>
#include <vector>

namespace toy3d::shader
{
    constexpr std::size_t max_shader_compile_job_programs = 4096u;
    constexpr std::size_t max_shader_compile_source_programs = 1024u;
    constexpr std::size_t max_shader_static_condition_nodes = 64u;

    enum class ShaderStaticConditionOperation : std::uint32_t
    {
        Equal,
        Profile,
        Capability,
        Not,
        All,
        Any
    };

    enum class ShaderStaticCapability : std::uint32_t
    {
        TextureCube = 1u,
        ReadOnlyTypedBuffer = 2u,
        Rgba16FloatSampled = 4u
    };

    constexpr std::uint32_t all_shader_static_capabilities = 7u;

    // A bounded postfix expression. Comparisons carry the same explicit kind
    // as Material selections; composite operations consume argument_count values.
    struct ShaderStaticConditionNode
    {
        ShaderStaticConditionOperation operation = ShaderStaticConditionOperation::Equal;
        ShaderPermutationSelection comparison;
        ShaderCompileProfile profile = ShaderCompileProfile::VulkanES31;
        ShaderStaticCapability capability = ShaderStaticCapability::TextureCube;
        std::uint32_t argument_count = 0u;
    };
    struct ShaderStaticCondition
    {
        // Empty means unconditional. Validation visits every node, including
        // branches whose value cannot affect the final result.
        std::vector<ShaderStaticConditionNode> nodes;
    };

    enum class ShaderEngineFeature : std::uint32_t
    {
        Lighting,
        Shadows,
        Environment
    };
    struct ShaderEngineFeatureDeclaration
    {
        ShaderEngineFeature feature = ShaderEngineFeature::Lighting;
        ShaderStaticCondition condition;
    };
    struct ShaderEngineFeatures
    {
        bool lighting = false;
        bool shadows = false;
        bool environment = false;
    };
    struct ShaderCompilePass
    {
        std::string name;
        ShaderPassRole role = ShaderPassRole::Global;
    };
    struct ShaderCompileSource
    {
        std::string name;
        ShaderUsage usage = ShaderUsage::Global;
        ShaderGeometryMode geometry = ShaderGeometryMode::None;
        std::uint32_t vertex_factory_support = 0u;
        ShaderPermutationDomain material_domain;
        std::vector<ShaderCompilePass> passes;
        std::vector<ShaderEngineFeatureDeclaration> features;
        ShaderStaticCondition supported_when;
        bool standard_tangent_input = false;
        bool declares_tangent_frame = false;
        ShaderStaticCondition tangent_frame_when;
    };
    struct ShaderCompilePolicy
    {
        ShaderTarget target = ShaderTarget::VulkanSpirV;
        ShaderCompileProfile profile = ShaderCompileProfile::VulkanES31;
        bool editor = true;
        std::uint32_t vertex_factory_support = all_vertex_factory_support;
        bool allow_pcf = true;
        bool allow_sky = true;
        std::uint32_t capabilities = 0u;
    };
    constexpr std::size_t max_shader_source_compile_request_bytes = 1024u * 1024u;

    // One immutable source job: typed configurations and a bounded engine policy.
    // Source-domain validation and deduplication happen before stage compilation.
    struct ShaderSourceCompileRequest
    {
        ShaderCompilePolicy policy;
        std::vector<std::vector<ShaderPermutationSelection>> configurations = {{}};
    };
    bool validate_shader_source_compile_request(const ShaderSourceCompileRequest& request, std::string& error);
    std::string serialize_shader_source_compile_request(const ShaderSourceCompileRequest& request);
    bool parse_shader_source_compile_request(const std::string& text, ShaderSourceCompileRequest& request,
                                             std::string& error);

    struct ShaderRequiredProgram
    {
        ShaderCompilePass pass;
        VertexFactoryType vertex_factory = VertexFactoryType::None;
        ShaderPermutation material_permutation;
        ShaderPermutation pass_permutation;
        ShaderEngineFeatures features;
    };
    struct ShaderCompilePlan
    {
        std::vector<ShaderRequiredProgram> required;
        std::size_t declared_programs = 0u;
        std::size_t filtered_programs = 0u;
        std::vector<std::string> filter_reasons;
        std::string error;
        bool succeeded() const;
    };

    // Persist conditions verbatim in bounded canonical form. Evaluation remains
    // shared by the compiler and artifact admission rather than duplicated in readers.
    std::string serialize_shader_static_condition(const ShaderStaticCondition& condition);
    bool parse_shader_static_condition(const std::string& text, ShaderStaticCondition& condition, std::string& error);

    bool evaluate_shader_static_condition(const ShaderStaticCondition& condition, const ShaderPermutationDomain& domain,
                                          const std::vector<ShaderPermutationSelection>& selections,
                                          const ShaderCompilePolicy& policy, bool& value, std::string& error);
    bool resolve_shader_engine_features(const ShaderCompileSource& source,
                                        const std::vector<ShaderPermutationSelection>& selections,
                                        const ShaderCompilePolicy& policy, ShaderEngineFeatures& features,
                                        std::string& error);
    ShaderPermutationDomain shader_pass_domain(ShaderPassRole role, const ShaderEngineFeatures& features);
    bool should_compile_permutation(const ShaderCompileSource& source, const ShaderCompilePass& pass,
                                    VertexFactoryType factory, const ShaderCompilePolicy& policy, std::string& reason);
    ShaderCompilePlan plan_shader_compilation(
        const ShaderCompileSource& source, const std::vector<std::vector<ShaderPermutationSelection>>& configurations,
        const ShaderCompilePolicy& policy);
} // namespace toy3d::shader
