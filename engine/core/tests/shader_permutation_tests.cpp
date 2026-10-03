#include "shader/shader_permutation.h"
#include "shader/shader_program_contract.h"
#include "shader/shader_map_index.h"
#include "shader/shader_compile_plan.h"
#include "shader/shader_build_settings.h"
#include "shader/shader_deployment.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace
{
    int failures = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    }

    toy3d::shader::ShaderPermutationDomain material_domain()
    {
        using namespace toy3d::shader;
        ShaderPermutationDomain domain;
        domain.dimensions = {
            {"USE_NORMAL_MAP", ShaderPermutationValueKind::Boolean, {}, false, {}, ShaderStageFlags::Pixel},
            {"LIGHTING_MODEL",
             ShaderPermutationValueKind::Enumeration,
             {"Unlit", "DefaultLit", "ClearCoat"},
             false,
             "DefaultLit",
             ShaderStageFlags::Pixel}};
        return domain;
    }

    void test_identity_and_projection()
    {
        using namespace toy3d::shader;
        auto domain = material_domain();
        const auto defaults = resolve_shader_permutation(domain, {});
        check(defaults.succeeded() && toy3d::sha256_to_hex(defaults.permutation->key) ==
                                          "6a67420b795afb9f008e5710bbc472fafe5bdea607dc887dccbb79fb838905a7",
              "Shared normalization must retain the independently specified identity bytes");
        check(make_shader_variant_id("USE_NORMAL_MAP") == 0xf17f0b805c4eae94ull &&
                  make_shader_enum_value_id(make_shader_variant_id("LIGHTING_MODEL"), "DefaultLit") ==
                      0x75c1de6eb41f9e57ull,
              "Variant and enum identities must retain their fixed golden values");
        std::vector<ShaderPermutationSelection> values = {
            {"USE_NORMAL_MAP", ShaderPermutationValueKind::Boolean, true, {}},
            {"LIGHTING_MODEL", ShaderPermutationValueKind::Enumeration, false, "ClearCoat"}};
        const auto selected = resolve_shader_permutation(domain, values);
        const auto domain_text = serialize_shader_permutation_domain(domain);
        ShaderPermutationDomain restored_domain;
        std::string domain_error;
        check(parse_shader_permutation_domain(domain_text, restored_domain, domain_error) &&
                  resolve_shader_permutation(restored_domain, selected.permutation->selections).permutation->key ==
                      selected.permutation->key,
              "Published domain and normalized selections must retain exact configuration identity");
        check(!parse_shader_permutation_domain(domain_text + "unknown\n", restored_domain, domain_error),
              "Permutation domain rejects trailing records");
        std::reverse(domain.dimensions.begin(), domain.dimensions.end());
        std::reverse(domain.dimensions.front().options.begin(), domain.dimensions.front().options.end());
        std::reverse(values.begin(), values.end());
        const auto reordered = resolve_shader_permutation(domain, values);
        check(selected.succeeded() && reordered.succeeded() &&
                  selected.permutation->key == reordered.permutation->key &&
                  selected.permutation->generated_prelude == reordered.permutation->generated_prelude,
              "Declaration, option and selection order must not affect identity or macros");
        const auto default_vs = resolve_shader_permutation(domain, {}, ShaderStageFlags::Vertex);
        const auto selected_vs = resolve_shader_permutation(domain, values, ShaderStageFlags::Vertex);
        const auto default_ps = resolve_shader_permutation(domain, {}, ShaderStageFlags::Pixel);
        const auto selected_ps = resolve_shader_permutation(domain, values, ShaderStageFlags::Pixel);
        check(default_vs.succeeded() && selected_vs.succeeded() &&
                  default_vs.permutation->key == selected_vs.permutation->key &&
                  selected_vs.permutation->records.empty() &&
                  selected_vs.permutation->generated_prelude.find("TOY3D_VARIANT_") == std::string::npos,
              "PS-only options must not change vertex-stage records or macros");
        check(default_ps.succeeded() && selected_ps.succeeded() &&
                  default_ps.permutation->key != selected_ps.permutation->key,
              "PS-only options must change pixel-stage identity");
        for (auto& dimension : domain.dimensions)
        {
            dimension.affected_passes = shader_pass_role_bit(ShaderPassRole::Forward);
        }
        const auto shadow =
            resolve_shader_permutation(domain, values, ShaderStageFlags::Pixel, ShaderPassRole::ShadowDepth);
        check(shadow.succeeded() && shadow.permutation->records.empty(),
              "Pass impact projects validated material options");
        const auto role_text = serialize_shader_permutation_domain(domain);
        check(parse_shader_permutation_domain(role_text, restored_domain, domain_error) &&
                  restored_domain.dimensions.front().affected_passes == shader_pass_role_bit(ShaderPassRole::Forward),
              "Domain serialization preserves stage and Pass impact");
        domain.dimensions.front().affected_passes = 0u;
        check(!resolve_shader_permutation(domain, {}).succeeded(), "Empty Pass impact rejects");
        domain.dimensions.front().affected_passes = shader_pass_role_bit(ShaderPassRole::Forward);
        domain.scope = ShaderPermutationScope::Pass;
        const auto pass = resolve_shader_permutation(domain, {});
        check(defaults.succeeded() && pass.succeeded() && defaults.permutation->key != pass.permutation->key &&
                  pass.permutation->generated_prelude.find("#define TOY3D_PASS_USE_NORMAL_MAP 0") != std::string::npos,
              "Engine Pass options and Material options must use separate identities and macro prefixes");
        const auto empty = resolve_shader_permutation(ShaderPermutationDomain{}, {});
        check(empty.succeeded() && empty.permutation->key == default_shader_permutation_key,
              "An empty domain has the versioned canonical identity");
    }

    void test_invalid_inputs()
    {
        using namespace toy3d::shader;
        const auto domain = material_domain();
        const std::vector<std::vector<ShaderPermutationSelection>> invalid_values = {
            {{"TYPO", ShaderPermutationValueKind::Boolean, true, {}}},
            {{"USE_NORMAL_MAP", ShaderPermutationValueKind::Enumeration, false, "true"}},
            {{"USE_NORMAL_MAP", ShaderPermutationValueKind::Boolean, true, "Conflicting"}},
            {{"LIGHTING_MODEL", ShaderPermutationValueKind::Enumeration, true, "DefaultLit"}},
            {{"LIGHTING_MODEL", ShaderPermutationValueKind::Enumeration, false, "Removed"}},
            {{"USE_NORMAL_MAP", ShaderPermutationValueKind::Boolean, true, {}},
             {"USE_NORMAL_MAP", ShaderPermutationValueKind::Boolean, false, {}}}};
        for (const auto& values : invalid_values)
        {
            const auto result = resolve_shader_permutation(domain, values, ShaderStageFlags::Vertex);
            check(!result.succeeded() && !result.permutation,
                  "Projection must not hide orphan, kind mismatch, conflicting or duplicate selections");
        }
        auto invalid = domain;
        invalid.dimensions[1].enum_default = "Removed";
        check(!resolve_shader_permutation(
                   invalid, {{"LIGHTING_MODEL", ShaderPermutationValueKind::Enumeration, false, "DefaultLit"}})
                   .succeeded(),
              "An explicit value must not mask an invalid enum default");
        invalid = domain;
        invalid.dimensions[1].options.push_back("DefaultLit");
        check(!resolve_shader_permutation(invalid, {}).succeeded(), "Duplicate enum values must fail");
        invalid = domain;
        invalid.dimensions.push_back(invalid.dimensions[0]);
        check(!resolve_shader_permutation(invalid, {}).succeeded(), "Duplicate dimension identities must fail");
        invalid = domain;
        invalid.dimensions[0].name = "TOY3D_RESERVED";
        check(!resolve_shader_permutation(invalid, {}).succeeded(), "Engine-reserved identifiers must fail");
        invalid.dimensions[0].name = "BAD-NAME";
        check(!resolve_shader_permutation(invalid, {}).succeeded(), "Macro-incompatible identifiers must fail");
        invalid = domain;
        invalid.dimensions[0].affected_stages = ShaderStageFlags::None;
        check(!resolve_shader_permutation(invalid, {}).succeeded(), "A dimension needs declared stage effects");
        invalid = domain;
        invalid.scope = static_cast<ShaderPermutationScope>(255u);
        check(!resolve_shader_permutation(invalid, {}).succeeded(), "Unknown owner scopes must fail");
        invalid = domain;
        invalid.dimensions[0].kind = static_cast<ShaderPermutationValueKind>(255u);
        check(!resolve_shader_permutation(invalid, {}).succeeded(), "Unknown kinds must fail");
        check(!resolve_shader_permutation(domain, {}, ShaderStageFlags::Vertex | ShaderStageFlags::Pixel).succeeded(),
              "Projection requires a single stage");
        // A_A conflicts with enum A's option A even when both are inactive.
        invalid.dimensions = {
            {"A", ShaderPermutationValueKind::Enumeration, {"A"}, false, "A", ShaderStageFlags::Pixel},
            {"A_A", ShaderPermutationValueKind::Boolean, {}, false, {}, ShaderStageFlags::Pixel}};
        invalid.scope = ShaderPermutationScope::Material;
        const auto collision = resolve_shader_permutation(invalid, {}, ShaderStageFlags::Vertex);
        check(!collision.succeeded() && !collision.permutation,
              "Inactive generated macro collisions must reject the entire configuration");
    }

    void test_program_contract()
    {
        using namespace toy3d::shader;
        std::string error;
        ShaderProgramContract contract;
        check(validate_shader_program_stages(contract, ShaderStageFlags::Compute, error),
              "Global programs admit compute without mesh geometry");
        contract = {ShaderUsage::Material, ShaderPassRole::Forward, ShaderGeometryMode::Custom,
                    VertexFactoryType::Local, all_vertex_factory_support};
        check(validate_shader_program_stages(contract, ShaderStageFlags::Vertex | ShaderStageFlags::Pixel, error),
              "Explicit Forward graphics contract must be accepted");
        check(!validate_shader_program_stages(contract, ShaderStageFlags::Vertex, error) &&
                  !validate_shader_program_stages(contract, ShaderStageFlags::Compute, error),
              "Forward cannot omit its pixel stage or use compute");
        contract.role = ShaderPassRole::ShadowDepth;
        check(validate_shader_program_stages(contract, ShaderStageFlags::Vertex, error),
              "Opaque ShadowDepth can omit its pixel stage");
        contract.role = ShaderPassRole::HitProxy;
        check(!validate_shader_program_stages(contract, ShaderStageFlags::Vertex, error),
              "HitProxy requires an ID-writing pixel stage");
        contract.vertex_factory = VertexFactoryType::GPUSkin;
        contract.vertex_factory_support = local_vertex_factory_support;
        check(!validate_shader_program_contract(contract, error), "Undeclared factories must fail");
        contract.vertex_factory_support = all_vertex_factory_support | 4u;
        check(!validate_shader_program_contract(contract, error), "Unknown factory support bits must fail");
        contract.vertex_factory_support = all_vertex_factory_support;
        contract.usage = ShaderUsage::Global;
        check(!validate_shader_program_contract(contract, error), "Global cannot own a mesh role/factory");
        contract.usage = ShaderUsage::MeshPass;
        contract.role = ShaderPassRole::Forward;
        check(!validate_shader_program_contract(contract, error), "MeshPass cannot declare Forward");
        contract.role = static_cast<ShaderPassRole>(255u);
        check(!validate_shader_program_contract(contract, error), "Unknown serialized roles must fail");
    }

    void test_map_index()
    {
        using namespace toy3d::shader;
        ShaderMapIndex index;
        index.shader_name = "Tests/IndexedSource";
        index.source_hash = toy3d::sha256("revision");
        index.permutation_key = default_shader_permutation_key;
        index.passes = {{"Display", ShaderPassRole::Forward}};
        index.programs = {{"Display",
                           {ShaderUsage::Material, ShaderPassRole::Forward, ShaderGeometryMode::Custom,
                            VertexFactoryType::Local, all_vertex_factory_support},
                           toy3d::sha256("local"),
                           toy3d::sha256("local content")},
                          {"Display",
                           {ShaderUsage::Material, ShaderPassRole::Forward, ShaderGeometryMode::Custom,
                            VertexFactoryType::GPUSkin, all_vertex_factory_support},
                           toy3d::sha256("skin"),
                           toy3d::sha256("skin content")}};
        std::string error;
        check(validate_shader_map_index(index, error), "Complete declared role/factory index must validate");
        const auto text = serialize_shader_map_index(index);
        std::reverse(index.programs.begin(), index.programs.end());
        check(serialize_shader_map_index(index) == text, "Index serialization must have deterministic query ordering");
        ShaderMapIndex parsed;
        check(parse_shader_map_index(text, parsed, error) && parsed.programs.size() == 2u,
              "Index must round-trip its full configuration and program records");
        const auto identity =
            calculate_shader_map_index_key(index.shader_name, index.target, index.profile, index.permutation_key);
        index.source_hash = toy3d::sha256("new revision");
        check(calculate_shader_map_index_key(index.shader_name, index.target, index.profile, index.permutation_key) ==
                      identity &&
                  serialize_shader_map_index(index) != text,
              "Configuration lookup identity is separate from source revision contents");
        index.programs.pop_back();
        check(!validate_shader_map_index(index, error), "Missing declared factory must reject the entire index");
        index = parsed;
        index.material_selections = {{"UNKNOWN", ShaderPermutationValueKind::Boolean, true, {}}};
        check(!validate_shader_map_index(index, error),
              "Unknown persisted material static selection rejects the index");
        index = parsed;
        index.passes.push_back({"Depth", ShaderPassRole::ShadowDepth});
        check(!validate_shader_map_index(index, error), "An entirely missing declared role must reject the index");
        check(!parse_shader_map_index(serialize_shader_map_index(index), parsed, error),
              "A recomputed digest cannot make missing role coverage valid");
        index.passes.pop_back();
        index.programs.push_back(index.programs.front());
        check(!validate_shader_map_index(index, error), "Duplicate queries must reject the index");
        auto damaged = text;
        damaged[damaged.find("Display")] = 'X';
        check(!parse_shader_map_index(damaged, parsed, error), "Index digest corruption must fail");
        check(!parse_shader_map_index(text + "unknown\n", parsed, error), "Unknown trailing index records must fail");
        damaged = text;
        damaged.replace(0u, std::string("shader_map_index 1").size(), "shader_map_index 0");
        check(!parse_shader_map_index(damaged, parsed, error), "Unsupported index versions must fail");
    }

    void test_bounds()
    {
        using namespace toy3d::shader;
        ShaderPermutationDomain domain;
        for (std::size_t index = 0u; index < max_shader_permutation_dimensions; ++index)
        {
            ShaderPermutationDimension dimension;
            dimension.name = "OPTION_" + std::to_string(index);
            domain.dimensions.push_back(dimension);
        }
        check(resolve_shader_permutation(domain, {}).succeeded(), "32 dimensions must be supported");
        domain.dimensions.push_back({"TOO_MANY"});
        check(!resolve_shader_permutation(domain, {}).succeeded(), "33 dimensions must fail");
        domain.dimensions.clear();
        ShaderPermutationDimension dimension;
        dimension.name = "MODE";
        dimension.kind = ShaderPermutationValueKind::Enumeration;
        dimension.enum_default = "VALUE_0";
        for (std::size_t index = 0u; index < max_shader_permutation_enum_values; ++index)
        {
            dimension.options.push_back("VALUE_" + std::to_string(index));
        }
        domain.dimensions.push_back(dimension);
        check(resolve_shader_permutation(domain, {}).succeeded(), "32 enum values must be supported");
        domain.dimensions[0].options.push_back("VALUE_32");
        check(!resolve_shader_permutation(domain, {}).succeeded(), "33 enum values must fail");
        domain.dimensions[0].options.clear();
        check(!resolve_shader_permutation(domain, {}).succeeded(), "Empty enum declarations must fail");
    }

    void test_compile_plan()
    {
        using namespace toy3d::shader;
        ShaderCompileSource source;
        source.name = "Tests/Planned";
        source.usage = ShaderUsage::Material;
        source.geometry = ShaderGeometryMode::Custom;
        source.vertex_factory_support = all_vertex_factory_support;
        source.material_domain.dimensions = {
            {"USE_LIGHTING", ShaderPermutationValueKind::Boolean, {}, true, {}, ShaderStageFlags::Pixel}};
        source.passes = {{"Color", ShaderPassRole::Forward},
                         {"Depth", ShaderPassRole::ShadowDepth},
                         {"Pick", ShaderPassRole::HitProxy}};
        ShaderStaticCondition lighting;
        ShaderStaticConditionNode comparison;
        comparison.comparison = {"USE_LIGHTING", ShaderPermutationValueKind::Boolean, true, {}};
        lighting.nodes.push_back(comparison);
        source.features = {{ShaderEngineFeature::Lighting, lighting},
                           {ShaderEngineFeature::Shadows, {}},
                           {ShaderEngineFeature::Environment, {}}};
        ShaderCompilePolicy policy;
        auto plan = plan_shader_compilation(source, {}, policy);
        check(plan.succeeded() && plan.required.size() == 12u && plan.declared_programs == 12u &&
                  plan.filtered_programs == 0u,
              "Lit Forward expands 2 factories x 2 shadow x 2 environment; other roles stay independent");
        ShaderStaticCondition decoded;
        std::string index_error;
        check(parse_shader_static_condition(serialize_shader_static_condition(lighting), decoded, index_error) &&
                  decoded.nodes.size() == 1u &&
                  !parse_shader_static_condition(serialize_shader_static_condition(lighting) + "unknown", decoded,
                                                 index_error),
              "Condition persistence preserves typed comparisons and rejects trailing data");
        ShaderMapIndex index;
        index.shader_name = source.name;
        index.source_hash = toy3d::sha256("feature revision");
        index.material_domain = source.material_domain;
        index.material_selections = plan.required.front().material_permutation.selections;
        index.permutation_key = plan.required.front().material_permutation.key;
        index.features = source.features;
        for (const auto& pass : source.passes)
        {
            index.passes.push_back({pass.name, pass.role});
        }
        for (const auto& required : plan.required)
        {
            const auto identity = required.pass.name +
                                  std::to_string(static_cast<std::uint32_t>(required.vertex_factory)) +
                                  toy3d::sha256_to_hex(required.pass_permutation.key);
            index.programs.push_back({required.pass.name,
                                      {source.usage, required.pass.role, source.geometry, required.vertex_factory,
                                       source.vertex_factory_support},
                                      toy3d::sha256(identity),
                                      toy3d::sha256(identity + "content"),
                                      required.pass_permutation.key,
                                      required.pass_permutation.selections});
        }
        check(validate_shader_map_index(index, index_error), "Index admits the exact planned engine permutations");
        ShaderMapIndex decoded_index;
        check(parse_shader_map_index(serialize_shader_map_index(index), decoded_index, index_error) &&
                  decoded_index.programs.size() == 12u && decoded_index.features.size() == 3u,
              "Index roundtrip preserves feature declarations and independent Pass identities");
        auto unlit_index = decoded_index;
        const auto unlit_plan = plan_shader_compilation(
            source, {{{"USE_LIGHTING", ShaderPermutationValueKind::Boolean, false, {}}}}, policy);
        unlit_index.permutation_key = unlit_plan.required.front().material_permutation.key;
        unlit_index.material_selections = unlit_plan.required.front().material_permutation.selections;
        unlit_index.programs.clear();
        for (const auto& required : unlit_plan.required)
        {
            const auto identity =
                required.pass.name + std::to_string(static_cast<std::uint32_t>(required.vertex_factory));
            unlit_index.programs.push_back({required.pass.name,
                                            {source.usage, required.pass.role, source.geometry, required.vertex_factory,
                                             source.vertex_factory_support},
                                            toy3d::sha256(identity),
                                            toy3d::sha256(identity + "content"),
                                            required.pass_permutation.key,
                                            required.pass_permutation.selections});
        }
        check(validate_shader_map_family({decoded_index, unlit_index}, index_error),
              "A complete family accepts different active engine domains from one source revision");
        unlit_index.source_hash = toy3d::sha256("other revision");
        check(!validate_shader_map_family({decoded_index, unlit_index}, index_error),
              "A family cannot mix source revisions");
        check(!validate_shader_map_family({decoded_index, decoded_index}, index_error),
              "A family cannot repeat a material configuration");
        index.programs.pop_back();
        check(!validate_shader_map_index(index, index_error), "A missing engine permutation rejects the whole index");
        index = decoded_index;
        index.programs.front().pass_selections.front().enum_value = "Unknown";
        check(!validate_shader_map_index(index, index_error), "Persisted Pass values must match their typed identity");
        policy.editor = false;
        plan = plan_shader_compilation(source, {}, policy);
        check(plan.succeeded() && plan.required.size() == 10u && plan.filtered_programs == 2u,
              "Player plans exclude HitProxy and explain filtered queries");
        policy.editor = true;
        policy.allow_pcf = false;
        policy.allow_sky = false;
        plan = plan_shader_compilation(source, {}, policy);
        check(plan.succeeded() && plan.required.size() == 6u && plan.filtered_programs == 6u,
              "Feature policy retains Off paths and filters unsupported algorithms");
        plan = plan_shader_compilation(source, {{{"USE_LIGHTING", ShaderPermutationValueKind::Boolean, false, {}}}},
                                       policy);
        check(plan.succeeded() && plan.required.size() == 6u && plan.required.front().pass_permutation.records.empty(),
              "Disabled Lighting omits engine domains rather than retaining irrelevant macros");
        auto unsupported = source;
        ShaderStaticConditionNode profile;
        profile.operation = ShaderStaticConditionOperation::Profile;
        profile.profile = ShaderCompileProfile::D3D12ShaderModel6;
        unsupported.supported_when.nodes = {profile};
        plan = plan_shader_compilation(unsupported, {}, policy);
        check(!plan.succeeded() && plan.required.empty(),
              "Unsupported requested profile/configuration fails before compilation");
        ShaderStaticCondition invalid;
        comparison.comparison.name = "UNKNOWN";
        invalid.nodes = {profile, comparison};
        ShaderStaticConditionNode all;
        all.operation = ShaderStaticConditionOperation::All;
        all.argument_count = 2u;
        invalid.nodes.push_back(all);
        bool value = true;
        std::string error;
        check(!evaluate_shader_static_condition(invalid, source.material_domain, {}, policy, value, error),
              "False condition branches do not hide undeclared option errors");
        all.argument_count = 3u;
        invalid.nodes = {profile, all};
        check(!evaluate_shader_static_condition(invalid, source.material_domain, {}, policy, value, error),
              "Malformed condition operand stacks reject without partial results");
        invalid.nodes.assign(max_shader_static_condition_nodes + 1u, profile);
        check(!evaluate_shader_static_condition(invalid, source.material_domain, {}, policy, value, error),
              "Condition read/evaluation is bounded before expansion");
        auto tangent_source = source;
        tangent_source.geometry = ShaderGeometryMode::Standard;
        tangent_source.declares_tangent_frame = true;
        tangent_source.tangent_frame_when = lighting;
        check(!plan_shader_compilation(tangent_source, {}, policy).succeeded(),
              "Standard geometry requirements reject missing fixed Tangent interpolation capability");
        tangent_source.standard_tangent_input = true;
        check(plan_shader_compilation(tangent_source, {}, policy).succeeded(),
              "Tangent requirement conditions are validated with the full static configuration");
        tangent_source.tangent_frame_when.nodes.front().comparison.name = "UNKNOWN";
        check(!plan_shader_compilation(tangent_source, {}, policy).succeeded(),
              "Geometry requirements cannot hide undeclared conditions");
        decoded_index.declares_tangent_frame = true;
        decoded_index.tangent_frame_when = lighting;
        check(parse_shader_map_index(serialize_shader_map_index(decoded_index), index, index_error) &&
                  index.declares_tangent_frame && index.tangent_frame_when.nodes.size() == 1u,
              "Index roundtrip persists Custom geometry requirements independently of stage inputs");
        source.features.erase(source.features.begin());
        check(!plan_shader_compilation(source, {}, policy).succeeded(),
              "Shadows/Environment require a declared Lighting feature");
        source.features.clear();
        std::vector<std::vector<ShaderPermutationSelection>> oversized(max_shader_compile_source_programs + 1u);
        check(!plan_shader_compilation(source, oversized, policy).succeeded(),
              "Oversized source configuration inputs reject before expansion");
        policy.vertex_factory_support = 0u;
        check(!plan_shader_compilation(source, {}, policy).succeeded(),
              "A plan cannot silently succeed with no required factory");
    }
    void test_build_settings()
    {
        using namespace toy3d::shader;
        auto settings = default_shader_build_settings();
        settings.policies.back().allow_pcf = false;
        settings.additional_configurations["Project/Surface/Test"] = {
            {{"NORMAL", ShaderPermutationValueKind::Boolean, true, {}}}};
        const auto text = serialize_shader_build_settings(settings);
        ShaderBuildSettings parsed;
        ShaderSourceCompileRequest request;
        std::string error;
        check(!text.empty() && parse_shader_build_settings(text, parsed, error) &&
                  make_shader_source_compile_request(parsed, "Project/Surface/Test", ShaderTarget::VulkanSpirV,
                                                     ShaderCompileProfile::VulkanES31, false, {{}}, request, error) &&
                  !request.policy.editor && !request.policy.allow_pcf && request.configurations.size() == 2u &&
                  request.configurations.back().front().boolean_value,
              "Build settings select Player policy and additional typed configurations");
        check(!parse_shader_build_settings(text + "unknown\n", parsed, error) && parsed.policies.size() == 2u,
              "Malformed build settings do not publish partial state");
        std::string windows_text;
        for (const char value : text)
        {
            if (value == '\n')
            {
                windows_text.push_back('\r');
            }
            windows_text.push_back(value);
        }
        check(parse_shader_build_settings(windows_text, parsed, error) &&
                  serialize_shader_build_settings(parsed) == text,
              "CRLF settings retain the same canonical policy and typed selections");
        check(!make_shader_source_compile_request(parsed, "Project/Surface/Test", ShaderTarget::D3D11Dxbc,
                                                  ShaderCompileProfile::D3D11FeatureLevel11_0, false, {}, request,
                                                  error),
              "Undeclared build profiles cannot fall back to another policy");
        settings.policies.push_back(settings.policies.front());
        check(serialize_shader_build_settings(settings).empty(), "Duplicate build profile policies reject");
        settings = default_shader_build_settings();
        settings.additional_configurations["Project/Test"] = {
            {{"NORMAL", ShaderPermutationValueKind::Enumeration, false, ""}}};
        check(serialize_shader_build_settings(settings).empty(), "Invalid extra typed configurations reject");
    }
    void test_deployment_manifest()
    {
        using namespace toy3d::shader;
        ShaderDeployment deployment;
        deployment.policy.editor = false;
        deployment.required_programs = 3u;
        deployment.sources.push_back(
            {"Project/Test", toy3d::sha256("source"), {default_shader_permutation_key, toy3d::sha256("configured")}});
        const auto text = serialize_shader_deployment(deployment);
        ShaderDeployment parsed;
        std::string error;
        check(!text.empty() && parse_shader_deployment(text, parsed, error) && !parsed.policy.editor &&
                  parsed.required_programs == 3u && parsed.sources.front().configurations.size() == 2u,
              "Deployment preserves exact source/configuration/Program coverage");
        check(!parse_shader_deployment(text + "unknown\n", parsed, error) && parsed.required_programs == 3u,
              "A damaged deployment cannot replace an existing complete manifest");
        deployment.sources.front().configurations.push_back(deployment.sources.front().configurations.front());
        check(serialize_shader_deployment(deployment).empty(), "Duplicate deployed configurations reject");
        deployment.sources.front().configurations.pop_back();
        deployment.required_programs = 1u;
        check(serialize_shader_deployment(deployment).empty(), "Required Program count cannot hide configurations");
    }
    void test_source_compile_request()
    {
        using namespace toy3d::shader;
        ShaderSourceCompileRequest request;
        request.policy.editor = false;
        request.policy.allow_pcf = false;
        request.configurations = {{},
                                  {{"NORMAL", ShaderPermutationValueKind::Boolean, true, {}},
                                   {"SURFACE", ShaderPermutationValueKind::Enumeration, false, "Masked"}}};
        const auto text = serialize_shader_source_compile_request(request);
        ShaderSourceCompileRequest parsed;
        std::string error;
        check(!text.empty() && parse_shader_source_compile_request(text, parsed, error) &&
                  parsed.configurations.size() == 2u && !parsed.policy.editor && !parsed.policy.allow_pcf &&
                  parsed.configurations[1][1].enum_value == "Masked",
              "Typed source job roundtrip preserves policy and configurations");
        check(!parse_shader_source_compile_request(text + "unknown\n", parsed, error) &&
                  parsed.configurations.size() == 2u,
              "Damaged request preserves previous output");
        request.configurations[1].push_back(request.configurations[1].front());
        check(!validate_shader_source_compile_request(request, error),
              "Duplicate typed options reject before source compilation");
        request.configurations = {{}};
        request.policy.capabilities = ~0u;
        check(!validate_shader_source_compile_request(request, error),
              "Unknown capability bits cannot enter compiler policy");
        request.policy = {};
        request.configurations.resize(max_shader_compile_source_programs + 1u);
        check(!validate_shader_source_compile_request(request, error), "Source configuration request input is bounded");
    }
} // namespace

int main()
{
    test_identity_and_projection();
    test_invalid_inputs();
    test_bounds();
    test_program_contract();
    test_map_index();
    test_compile_plan();
    test_source_compile_request();
    test_build_settings();
    test_deployment_manifest();
    return failures == 0 ? 0 : 1;
}
