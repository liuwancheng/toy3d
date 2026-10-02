#include "shader/shader_permutation.h"
#include "shader/shader_program_contract.h"
#include "shader/shader_map_index.h"

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
} // namespace

int main()
{
    test_identity_and_projection();
    test_invalid_inputs();
    test_bounds();
    test_program_contract();
    test_map_index();
    return failures == 0 ? 0 : 1;
}
