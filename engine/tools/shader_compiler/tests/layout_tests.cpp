#include "codegen/binding_codegen.h"
#include "codegen/cpp_identifier.h"
#include "codegen/shader_parameters_codegen.h"
#include "codegen/shader_parameters_writer.h"
#include "file_system/native_platform_file.h"
#include "hash/sha256.h"
#include "format/shader_map_entry.h"
#include "frontend/shader_parser.h"
#include "layout/binding_allocator.h"
#include "layout/shader_layout.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    bool has_diagnostic(const std::vector<toy3d::shader::Diagnostic>& diagnostics, toy3d::shader::DiagnosticCode code)
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(),
                           [&](const toy3d::shader::Diagnostic& diagnostic) { return diagnostic.code == code; });
    }

    void test_constant_buffer_data_layout_hash()
    {
        using namespace toy3d::shader;
        const std::vector<ReflectedConstantMember> members = {
            {11u, "matrix", ShaderValueType::Float32x4x4, 0u, 64u, 0u, 16u},
            {12u, "weights", ShaderValueType::Float32x4, 64u, 32u, 16u, 0u}};
        std::vector<ReflectedConstantMember> reordered = {members[1], members[0]};
        const ShaderDataLayoutHash baseline = calculate_constant_buffer_data_layout_hash(
            BindingGroup::View, 10u, 96u, members);
        check(baseline == calculate_constant_buffer_data_layout_hash(BindingGroup::View, 10u, 96u, reordered),
              "constant-buffer data layout hash must be declaration-order independent");

        std::vector<ReflectedConstantMember> changed = members;
        changed[1].offset = 60u;
        check(baseline != calculate_constant_buffer_data_layout_hash(BindingGroup::View, 10u, 96u, changed),
              "constant-buffer data layout hash must include member offsets even when total size is unchanged");
        changed = members;
        changed[0].matrix_stride = 12u;
        check(baseline != calculate_constant_buffer_data_layout_hash(BindingGroup::View, 10u, 96u, changed),
              "constant-buffer data layout hash must include matrix stride");
        changed = members;
        changed[1].array_stride = 32u;
        check(baseline != calculate_constant_buffer_data_layout_hash(BindingGroup::View, 10u, 96u, changed),
              "constant-buffer data layout hash must include array stride");
        check(baseline != calculate_constant_buffer_data_layout_hash(BindingGroup::View, 10u, 96u, members,
                                                                      toy_shader_abi_version + 1u),
              "constant-buffer data layout hash must include the Shader ABI version");
    }

    toy3d::shader::LogicalLayoutResult compile_source(const std::string& source)
    {
        const toy3d::shader::ParseResult parsed = toy3d::shader::parse_shader(source, "layout_test.shader");
        check(parsed.succeeded(), "layout test Shader source must parse");
        if (!parsed.asset)
        {
            return {};
        }
        return toy3d::shader::compile_logical_layout(*parsed.asset);
    }

    const char* shader_source_a = R"(
Shader "Tests/Layout"
{
    Version 1
    Properties
    {
        base_color ("Base Color", Color) = (1.0, 0.5, 0.25, 1.0)
        roughness ("Roughness", Float) = 0.5
        base_color_texture ("Base Color Texture", Texture2D) = "white"
        material_sampler ("Material Sampler", Sampler) = LinearWrap
    }
    Parameters
    {
        Pass
        {
            exposure_ev : Float = 0.0
            projection : Float4x4
        }
    }
    Resources
    {
        Pass
        {
            scene_sampler : Sampler = LinearClamp
            scene_texture : Texture2D<Float4>
            output_values : RWStructuredBuffer<Float4>
        }
        Object
        {
            transforms : StructuredBuffer<Float4x4>
        }
    }
    Pass "Forward"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        ENDHLSL
    }
}
)";

    const char* shader_source_reordered_resources = R"(
Shader "Tests/Layout"
{
    Version 1
    Properties
    {
        base_color ("Base Color", Color) = (1.0, 0.5, 0.25, 1.0)
        roughness ("Roughness", Float) = 0.5
        material_sampler ("Material Sampler", Sampler) = LinearWrap
        base_color_texture ("Base Color Texture", Texture2D) = "white"
    }
    Parameters
    {
        Pass
        {
            exposure_ev : Float = 0.0
            projection : Float4x4
        }
    }
    Resources
    {
        Object { transforms : StructuredBuffer<Float4x4> }
        Pass
        {
            scene_texture : Texture2D<Float4>
            scene_sampler : Sampler = LinearClamp
            output_values : RWStructuredBuffer<Float4>
        }
    }
    Pass "Forward"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        ENDHLSL
    }
}
)";

    void test_sha256_and_parameter_id()
    {
        const toy3d::Sha256Hash hash = toy3d::sha256("abc");
        const toy3d::Sha256Hash expected = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                                    0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                                    0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
        check(hash == expected, "SHA-256 must match the standard abc test vector");
        const toy3d::shader::ShaderParameterId id = toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::Material, toy3d::shader::ShaderParameterCategory::Constant, "base_color");
        check(id == 1049192153525424859ull, "ShaderParameterId must match the v1 golden FNV-1a value");
        check(id != toy3d::shader::make_shader_parameter_id(toy3d::shader::BindingGroup::View,
                                                            toy3d::shader::ShaderParameterCategory::Constant,
                                                            "base_color"),
              "Binding Group must participate in ShaderParameterId");
    }

    void test_toy_shader_abi_packing()
    {
        using namespace toy3d::shader;
        const std::vector<ConstantMemberInput> inputs = {{"normal", ShaderValueType::Float32x3},
                                                         {"weight", ShaderValueType::Float32},
                                                         {"uv", ShaderValueType::Float32x2},
                                                         {"transform", ShaderValueType::Float32x4x4},
                                                         {"values", ShaderValueType::Float32, 3}};
        const ConstantBufferPackResult packed = pack_constant_buffer(BindingGroup::Material, inputs);
        check(packed.succeeded(), "representative ToyShaderABI buffer must pack");
        if (packed.layout)
        {
            const auto& members = packed.layout->members;
            check(members[0].offset == 0 && members[1].offset == 12, "float3 + float must share one register");
            check(members[2].offset == 16, "float2 must honor 8-byte alignment");
            check(members[3].offset == 32 && members[3].matrix_stride == 16,
                  "matrix columns must use 16-byte registers");
            check(members[4].offset == 96 && members[4].array_stride == 16 && members[4].size == 48,
                  "scalar arrays must use a 16-byte stride");
            check(packed.layout->size == 144, "constant buffer size must align to 16 bytes");
        }
        check(structured_element_stride(ResourceElementType::Float3) == 16,
              "StructuredBuffer Float3 stride must include padding");
        check(structured_element_stride(ResourceElementType::Float4x4) == 64,
              "StructuredBuffer matrix stride must be column based");

        std::vector<ConstantMemberInput> too_large;
        for (std::uint32_t index = 0; index < 1025; ++index)
        {
            too_large.push_back({"value_" + std::to_string(index), ShaderValueType::Float32x4});
        }
        const ConstantBufferPackResult rejected = pack_constant_buffer(BindingGroup::Material, too_large);
        check(!rejected.succeeded() &&
                  has_diagnostic(rejected.diagnostics, DiagnosticCode::ConstantBufferSizeLimitExceeded),
              "constant buffers larger than 16 KiB must fail diagnostically");
    }

    void test_logical_layout_determinism()
    {
        using namespace toy3d::shader;
        const LogicalLayoutResult first = compile_source(shader_source_a);
        const LogicalLayoutResult reordered = compile_source(shader_source_reordered_resources);
        check(first.succeeded() && reordered.succeeded(), "logical layout inputs must compile");
        if (!first.layout || !reordered.layout)
            return;
        check(first.layout->logical_layout_hash == reordered.layout->logical_layout_hash,
              "independent resource source order must not change logical layout hash");
        check(first.layout->resources.size() == 6,
              "Properties and Resources must merge into one logical resource schema");
        const ShaderParameterGroupInput global_input = builtin_shader_parameter_input(BindingGroup::Global);
        const ShaderParameterGroupInput view_input = builtin_shader_parameter_input(BindingGroup::View);
        const ShaderParameterGroupInput object_input = builtin_shader_parameter_input(BindingGroup::Object);
        check(global_input.group == BindingGroup::Global && global_input.constant_members.empty() &&
                  view_input.group == BindingGroup::View && view_input.constant_members.size() == 8u &&
                  object_input.group == BindingGroup::Object && object_input.constant_members.size() == 1u,
              "Global/View/Object schemas must enter the same normalized group input used by declared groups");
        check(
            first.layout->constant_buffers.size() == 4,
            "canonical View/Object, Pass Parameters and numeric Material properties must produce four group cbuffers");
        const auto view_buffer =
            std::find_if(first.layout->constant_buffers.begin(), first.layout->constant_buffers.end(),
                         [](const ConstantBufferLayout& buffer) { return buffer.group == BindingGroup::View; });
        const auto material_buffer =
            std::find_if(first.layout->constant_buffers.begin(), first.layout->constant_buffers.end(),
                         [](const ConstantBufferLayout& buffer) { return buffer.group == BindingGroup::Material; });
        const auto object_buffer =
            std::find_if(first.layout->constant_buffers.begin(), first.layout->constant_buffers.end(),
                         [](const ConstantBufferLayout& buffer) { return buffer.group == BindingGroup::Object; });
        const auto pass_buffer =
            std::find_if(first.layout->constant_buffers.begin(), first.layout->constant_buffers.end(),
                         [](const ConstantBufferLayout& buffer) { return buffer.group == BindingGroup::Pass; });
        check(view_buffer != first.layout->constant_buffers.end() && view_buffer->members.size() == 8 &&
                  view_buffer->members[2].name == "toy_view_projection" && view_buffer->members[2].offset == 128 &&
                  view_buffer->members[2].matrix_stride == 16,
              "canonical View schema must preserve the real view-projection ToyShaderABI path");
        check(material_buffer != first.layout->constant_buffers.end() && material_buffer->members[0].offset == 0,
              "constant member order must follow Material property source order");
        check(object_buffer != first.layout->constant_buffers.end() && object_buffer->members.size() == 1 &&
                  object_buffer->members[0].name == "toy_object_to_world" &&
                  object_buffer->members[0].matrix_stride == 16,
              "canonical Object schema must preserve the object-to-world ToyShaderABI path");
        check(pass_buffer != first.layout->constant_buffers.end() && pass_buffer->members.size() == 2 &&
                  pass_buffer->members[0].name == "exposure_ev" && pass_buffer->members[0].offset == 0 &&
                  pass_buffer->members[1].name == "projection" && pass_buffer->members[1].offset == 16 &&
                  pass_buffer->members[1].matrix_stride == 16,
              "Pass Parameters must pack in source order into one ToyShaderABI constant buffer");
        if (pass_buffer != first.layout->constant_buffers.end())
        {
            check(pass_buffer->members[0].default_value.size() == 4 &&
                      pass_buffer->members[1].default_value.size() == 64 &&
                      std::all_of(pass_buffer->members[1].default_value.begin(),
                                  pass_buffer->members[1].default_value.end(),
                                  [](std::uint8_t value) { return value == 0; }),
                  "omitted Pass parameter defaults must publish zero-initialized bytes");
        }

        std::string changed_default = shader_source_a;
        const std::size_t default_position = changed_default.find("roughness (\"Roughness\", Float) = 0.5");
        changed_default.replace(default_position, std::string("roughness (\"Roughness\", Float) = 0.5").size(),
                                "roughness (\"Roughness\", Float) = 0.75");
        const LogicalLayoutResult changed = compile_source(changed_default);
        check(changed.succeeded(), "changed-default layout must compile");
        if (changed.layout)
        {
            check(first.layout->logical_layout_hash == changed.layout->logical_layout_hash,
                  "defaults must not enter logical layout hash");
            check(first.layout->parameter_schema_hash != changed.layout->parameter_schema_hash,
                  "defaults must enter parameter schema hash");
        }

        std::string reordered_constants = shader_source_a;
        const std::string original_constants = "        base_color (\"Base Color\", Color) = (1.0, 0.5, 0.25, 1.0)\n"
                                               "        roughness (\"Roughness\", Float) = 0.5";
        const std::string swapped_constants = "        roughness (\"Roughness\", Float) = 0.5\n"
                                              "        base_color (\"Base Color\", Color) = (1.0, 0.5, 0.25, 1.0)";
        const std::size_t constants_position = reordered_constants.find(original_constants);
        reordered_constants.replace(constants_position, original_constants.size(), swapped_constants);
        const LogicalLayoutResult reordered_constant_layout = compile_source(reordered_constants);
        check(reordered_constant_layout.succeeded(), "reordered constant layout must compile");
        if (reordered_constant_layout.layout)
        {
            check(first.layout->logical_layout_hash == reordered_constant_layout.layout->logical_layout_hash &&
                      first.layout->parameter_schema_hash ==
                          reordered_constant_layout.layout->parameter_schema_hash,
                  "constant declaration order must not change canonical offsets or schema identity");
        }

        const ShaderParameterSchema first_schema = make_shader_parameter_schema(*first.layout);
        const ShaderParameterSchema repeated_schema = make_shader_parameter_schema(*first.layout);
        check(serialize_shader_parameter_schema(first_schema) == serialize_shader_parameter_schema(repeated_schema),
              "the same normalized input must reproduce byte-identical schema records");
        const std::vector<std::pair<BindingGroup, std::string>> group_identity_golden = {
            {BindingGroup::Global, "a7c2b48921c1ca26d7d727e64e5775ae2c6cac9319e8726046b8dc0377661ac7"},
            {BindingGroup::View, "ef98b24a1c90b0357b7ad7d36e6fa6de000ec03eed0b581b5c6c50b54d2a5bf9"},
            {BindingGroup::Pass, "136393ece4a6e3f0b8b7fa2a67d02503406cdb85567db96f3844f08bce461f42"},
            {BindingGroup::Material, "dc08ed54b20ac5401825c3f4f40d234e81ccdce791342b4f6f43b87769b7997f"},
            {BindingGroup::Object, "c6c22f2e79e55c0b85503aa31dda1c8472d6d1e7e3f2960313f66b3c43a9d8fe"}};
        for (const auto& identity : group_identity_golden)
        {
            check(toy3d::sha256_to_hex(calculate_shader_parameter_group_identity(first_schema, identity.first)) ==
                      identity.second,
                  "each Global/View/Pass/Material/Object schema identity must retain its golden value");
        }

        std::string invalid_default = shader_source_a;
        const std::size_t invalid_default_position = invalid_default.find("roughness (\"Roughness\", Float) = 0.5");
        invalid_default.replace(invalid_default_position, std::string("roughness (\"Roughness\", Float) = 0.5").size(),
                                "roughness (\"Roughness\", Float) = (0.5, 1.0)");
        const LogicalLayoutResult rejected_default = compile_source(invalid_default);
        check(!rejected_default.succeeded() &&
                  has_diagnostic(rejected_default.diagnostics, DiagnosticCode::InvalidDefaultValue),
              "default values incompatible with the logical type must fail before codegen");
    }

    void test_active_layout_allocators_and_codegen()
    {
        using namespace toy3d::shader;
        const LogicalLayoutResult logical = compile_source(shader_source_a);
        if (!logical.layout)
            return;
        const std::vector<ParameterUsage> usage = {
            {"exposure_ev", ShaderStageFlags::Pixel},        {"base_color", ShaderStageFlags::Pixel},
            {"base_color_texture", ShaderStageFlags::Pixel}, {"material_sampler", ShaderStageFlags::Pixel},
            {"scene_texture", ShaderStageFlags::Pixel},      {"scene_sampler", ShaderStageFlags::Pixel},
            {"transforms", ShaderStageFlags::Vertex}};
        const ActiveLayoutResult active = build_active_layout(*logical.layout, usage);
        check(active.succeeded(), "known Program usage must build an active layout");
        if (!active.layout)
            return;
        check(active.layout->bindings.size() == 7, "two used cbuffers plus five used resources must remain active");
        std::size_t constant_buffer_count = 0;
        for (const ActiveBinding& binding : active.layout->bindings)
        {
            if (binding.constant_buffer)
                ++constant_buffer_count;
        }
        check(constant_buffer_count == 2, "using a member must retain each complete logical group cbuffer");

        const TargetBindingResult d3d =
            allocate_target_bindings(*active.layout, ShaderTarget::D3D11Dxbc, TargetBindingLimits::d3d11_sm5());
        const TargetBindingResult d3d12 =
            allocate_target_bindings(*active.layout, ShaderTarget::D3D12Dxil, TargetBindingLimits::d3d12_sm6());
        const TargetBindingResult vulkan = allocate_target_bindings(*active.layout, ShaderTarget::VulkanSpirV,
                                                                    TargetBindingLimits::vulkan_portable_v1());
        check(d3d.succeeded() && d3d12.succeeded() && vulkan.succeeded(), "all three target mappings must allocate");
        if (!d3d.layout || !d3d12.layout || !vulkan.layout)
            return;
        check(d3d.layout->target_binding_hash != vulkan.layout->target_binding_hash,
              "target-native mapping must have a target-specific hash");
        check(d3d.layout->target_binding_hash != d3d12.layout->target_binding_hash,
              "D3D11 and D3D12 identities must remain distinct even when slots match");

        const auto d3d_scene_texture =
            std::find_if(d3d.layout->bindings.begin(), d3d.layout->bindings.end(), [](const NativeBinding& binding)
                         { return binding.name == "scene_texture" && binding.stages == ShaderStageFlags::Pixel; });
        const auto d3d_material_texture =
            std::find_if(d3d.layout->bindings.begin(), d3d.layout->bindings.end(), [](const NativeBinding& binding)
                         { return binding.name == "base_color_texture" && binding.stages == ShaderStageFlags::Pixel; });
        check(d3d_scene_texture != d3d.layout->bindings.end() && d3d_scene_texture->register_index == 0,
              "D3D t registers must start with the first logical group");
        check(d3d_material_texture != d3d.layout->bindings.end() && d3d_material_texture->register_index == 1,
              "D3D t registers must remain compact across logical groups");

        const auto scene_texture =
            std::find_if(vulkan.layout->bindings.begin(), vulkan.layout->bindings.end(),
                         [](const NativeBinding& binding) { return binding.name == "scene_texture"; });
        const auto material_texture =
            std::find_if(vulkan.layout->bindings.begin(), vulkan.layout->bindings.end(),
                         [](const NativeBinding& binding) { return binding.name == "base_color_texture"; });
        check(scene_texture != vulkan.layout->bindings.end() && scene_texture->descriptor_set == 1,
              "Pass resources must map to Vulkan set 1");
        check(material_texture != vulkan.layout->bindings.end() && material_texture->descriptor_set == 2,
              "Material resources must map to Vulkan set 2");
        if (scene_texture != vulkan.layout->bindings.end())
        {
            check(scene_texture->descriptor_binding == 1,
                  "Pass resources must follow the Pass constant buffer in a compact Vulkan set");
        }

        const BindingCodegenResult d3d_hlsl =
            generate_binding_hlsl(*logical.layout, *d3d.layout, ShaderStageFlags::Pixel);
        const BindingCodegenResult vulkan_hlsl =
            generate_binding_hlsl(*logical.layout, *vulkan.layout, ShaderStageFlags::Pixel);
        check(d3d_hlsl.succeeded() && vulkan_hlsl.succeeded(), "target-specific binding HLSL must generate");
        if (d3d_hlsl.source && vulkan_hlsl.source)
        {
            check(d3d_hlsl.source->find("packoffset(c0)") != std::string::npos,
                  "D3D HLSL must emit explicit packoffset");
            check(d3d_hlsl.source->find("register(t0)") != std::string::npos,
                  "D3D HLSL must emit compact class-local registers");
            check(vulkan_hlsl.source->find("[[vk::binding(") != std::string::npos,
                  "Vulkan HLSL must emit explicit descriptor decoration");
            check(vulkan_hlsl.source->find("/Generated/ToyBindings.hlsli") != std::string::npos,
                  "generated HLSL must preserve its virtual diagnostic path");
            check(d3d_hlsl.compile_key != vulkan_hlsl.compile_key,
                  "target and generated mapping must enter the compile key");
        }

        TargetBindingLimits insufficient = TargetBindingLimits::vulkan_portable_v1();
        insufficient.per_stage_descriptors[1].samplers = 0;
        const TargetBindingResult rejected =
            allocate_target_bindings(*active.layout, ShaderTarget::VulkanSpirV, insufficient);
        check(!rejected.succeeded() && has_diagnostic(rejected.diagnostics, DiagnosticCode::BindingLimitExceeded),
              "profile binding limit failures must be diagnostic");

        const ActiveLayoutResult unknown =
            build_active_layout(*logical.layout, {{"not_declared", ShaderStageFlags::Pixel}});
        check(!unknown.succeeded() && has_diagnostic(unknown.diagnostics, DiagnosticCode::UnknownParameterUsage),
              "unknown active resources must fail before target allocation");

        const ActiveLayoutResult vertex_uav =
            build_active_layout(*logical.layout, {{"output_values", ShaderStageFlags::Vertex}});
        check(vertex_uav.succeeded(), "known vertex storage usage must reach target capability validation");
        if (vertex_uav.layout)
        {
            const TargetBindingResult d3d11_rejected =
                allocate_target_bindings(*vertex_uav.layout, ShaderTarget::D3D11Dxbc, TargetBindingLimits::d3d11_sm5());
            check(!d3d11_rejected.succeeded() &&
                      has_diagnostic(d3d11_rejected.diagnostics, DiagnosticCode::BindingLimitExceeded),
                  "D3D11 SM5 vertex UAV usage must be rejected by stage limits");
        }
    }

    void test_cpp_identifier_mapping()
    {
        using namespace toy3d::shader;
        ShaderAsset asset;
        asset.name = "Toy3d/PostProcess/Tonemap";
        asset.passes.push_back({"Tonemap"});
        asset.parameters.push_back({BindingGroup::Pass, "exposure_ev", ShaderValueType::Float32});
        Resource scene_texture;
        scene_texture.group = BindingGroup::Pass;
        scene_texture.name = "scene_texture";
        asset.resources.push_back(scene_texture);

        const CppIdentifierMappingResult mapped = map_shader_parameter_cpp_identifiers(asset);
        check(mapped.succeeded(), "valid Shader names must map to deterministic C++ identifiers");
        if (mapped.identifiers && !mapped.identifiers->passes.empty())
        {
            const CppShaderPassIdentifiers& pass = mapped.identifiers->passes.front();
            check(mapped.identifiers->version == shader_parameters_cpp_identifier_version &&
                      mapped.identifiers->header_stem == "toy3d_postprocess_tonemap" &&
                      mapped.identifiers->shader_type_stem == "Toy3dPostProcessTonemap" &&
                      pass.parameters_type == "TonemapPassParameters" &&
                      pass.metadata_accessor == "shader_parameters_metadata" &&
                      pass.encode_function == "encode_shader_parameters" && pass.fields.size() == 2u &&
                      pass.fields[0].field_name == "exposure_ev" && pass.fields[1].field_name == "scene_texture",
                  "identifier mapping version must lock type, field, accessor, and encode names");
        }

        ShaderAsset keyword = asset;
        keyword.parameters.front().name = "class";
        const CppIdentifierMappingResult keyword_result = map_shader_parameter_cpp_identifiers(keyword);
        check(!keyword_result.succeeded() &&
                  has_diagnostic(keyword_result.diagnostics, DiagnosticCode::InvalidGeneratedIdentifier),
              "C++17 keywords must fail identifier mapping");

        ShaderAsset illegal = asset;
        illegal.resources.front().name = "scene-texture";
        const CppIdentifierMappingResult illegal_result = map_shader_parameter_cpp_identifiers(illegal);
        check(!illegal_result.succeeded() &&
                  has_diagnostic(illegal_result.diagnostics, DiagnosticCode::InvalidGeneratedIdentifier),
              "characters outside the versioned ASCII identifier grammar must fail mapping");

        ShaderAsset case_collision = asset;
        case_collision.parameters.front().name = "SceneTexture";
        case_collision.resources.front().name = "scenetexture";
        const CppIdentifierMappingResult case_collision_result =
            map_shader_parameter_cpp_identifiers(case_collision);
        check(!case_collision_result.succeeded() &&
                  has_diagnostic(case_collision_result.diagnostics, DiagnosticCode::GeneratedIdentifierConflict) &&
                  !case_collision_result.identifiers,
              "case-normalized field collisions must fail instead of receiving a numeric suffix");

        ShaderAsset duplicate_resource = asset;
        duplicate_resource.resources.push_back(duplicate_resource.resources.front());
        const CppIdentifierMappingResult duplicate_resource_result =
            map_shader_parameter_cpp_identifiers(duplicate_resource);
        check(!duplicate_resource_result.succeeded() &&
                  has_diagnostic(duplicate_resource_result.diagnostics,
                                 DiagnosticCode::GeneratedIdentifierConflict),
              "duplicate resource names must fail independently at the C++ codegen boundary");

        ShaderAsset pass_collision = asset;
        ShaderPass normalized_collision;
        normalized_collision.name = "tonemap";
        pass_collision.passes.push_back(normalized_collision);
        const CppIdentifierMappingResult pass_collision_result =
            map_shader_parameter_cpp_identifiers(pass_collision);
        check(!pass_collision_result.succeeded() &&
                  has_diagnostic(pass_collision_result.diagnostics,
                                 DiagnosticCode::GeneratedIdentifierConflict),
              "case-normalized type collisions must fail instead of receiving a numeric suffix");
    }

    void test_shader_parameters_cpp_codegen()
    {
        using namespace toy3d::shader;
        const char* tonemap_source = R"(
Shader "Toy3d/PostProcess/Tonemap"
{
    Version 1
    Parameters { Pass { exposure_ev : Float = 0.0 } }
    Resources { Pass { scene_color : Texture2D<Float4> scene_sampler : Sampler = LinearClamp } }
    Pass "Tonemap"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        ENDHLSL
    }
}
)";
        const ParseResult tonemap_parsed = parse_shader(tonemap_source, "tonemap_codegen.shader");
        check(tonemap_parsed.succeeded(), "Tonemap codegen fixture must parse");
        if (!tonemap_parsed.asset)
            return;
        const LogicalLayoutResult tonemap_layout = compile_logical_layout(*tonemap_parsed.asset);
        check(tonemap_layout.succeeded(), "Tonemap codegen fixture must produce a canonical schema");
        if (!tonemap_layout.layout)
            return;
        const ShaderParametersCodegenResult tonemap =
            generate_shader_parameters_header(*tonemap_parsed.asset, *tonemap_layout.layout);
        check(tonemap.succeeded() && tonemap.output_name == "toy3d_postprocess_tonemap.generated.h",
              "Tonemap must generate one deterministic header unit");
        if (tonemap.source)
        {
            const ShaderParameterSchema schema = make_shader_parameter_schema(*tonemap_layout.layout);
            check(tonemap.source->find("struct TonemapPassParameters") != std::string::npos &&
                      tonemap.source->find("float exposure_ev{};") != std::string::npos &&
                      tonemap.source->find("RHITextureViewRef scene_color{};") != std::string::npos &&
                      tonemap.source->find("RHISamplerRef scene_sampler{};") != std::string::npos,
                  "Tonemap generated fields must use the expected strong C++ types");
            check(tonemap.source->find("inline const ShaderParametersMetadata& shader_parameters_metadata") !=
                          std::string::npos &&
                      tonemap.source->find("inline void encode_shader_parameters") != std::string::npos &&
                      tonemap.source->find(toy3d::sha256_to_hex(schema.schema_identity)) != std::string::npos &&
                      tonemap.source->find(toy3d::sha256_to_hex(
                          calculate_shader_parameter_group_identity(schema, BindingGroup::Pass))) != std::string::npos,
                  "Tonemap metadata must carry the canonical full and Pass schema identities");
        }

        const char* imgui_source = R"(
Shader "Toy3d/UI/ImGui"
{
    Version 1
    Parameters { Pass { projection : Float4x4 } }
    Resources { Pass { font_texture : Texture2D<Float4> font_sampler : Sampler = LinearClamp } }
    Pass "ImGui"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        ENDHLSL
    }
}
)";
        const ParseResult imgui_parsed = parse_shader(imgui_source, "imgui_codegen.shader");
        check(imgui_parsed.succeeded(), "ImGui codegen fixture must parse");
        if (imgui_parsed.asset)
        {
            const LogicalLayoutResult imgui_layout = compile_logical_layout(*imgui_parsed.asset);
            if (imgui_layout.layout)
            {
                const ShaderParametersCodegenResult imgui =
                    generate_shader_parameters_header(*imgui_parsed.asset, *imgui_layout.layout);
                const ShaderParameterSchema imgui_schema = make_shader_parameter_schema(*imgui_layout.layout);
                check(imgui.succeeded() && imgui.source &&
                          imgui.source->find("struct ImGuiPassParameters") != std::string::npos &&
                          imgui.source->find("Matrix4 projection = Matrix4::zero();") != std::string::npos &&
                          imgui.source->find("RHITextureViewRef font_texture{};") != std::string::npos &&
                          imgui.source->find("RHISamplerRef font_sampler{};") != std::string::npos &&
                          imgui.source->find(toy3d::sha256_to_hex(imgui_schema.schema_identity)) != std::string::npos &&
                          imgui.source->find(toy3d::sha256_to_hex(calculate_shader_parameter_group_identity(
                              imgui_schema, BindingGroup::Pass))) != std::string::npos,
                      "ImGui generated parameters must preserve matrix and resource field types");
            }
        }

        const ShaderParametersCodegenResult builtin = generate_builtin_shader_parameters_header();
        check(builtin.succeeded() && builtin.output_name == "builtin_shader_parameters.generated.h" && builtin.source,
              "builtin schemas must generate one shared header unit");
        if (builtin.source)
        {
            LogicalShaderLayout builtin_layout;
            for (BindingGroup group : {BindingGroup::View, BindingGroup::Object})
            {
                const ShaderParameterGroupInput input = builtin_shader_parameter_input(group);
                ConstantBufferPackResult packed = pack_constant_buffer(group, input.constant_members);
                if (packed.layout)
                    builtin_layout.constant_buffers.push_back(std::move(*packed.layout));
            }
            const ShaderParameterSchema builtin_schema = make_shader_parameter_schema(builtin_layout);
            check(builtin.source->find("struct ViewShaderParameters") != std::string::npos &&
                      builtin.source->find("Matrix4 toy_view = Matrix4::zero();") != std::string::npos &&
                      builtin.source->find("Vector3 toy_camera_position{};") != std::string::npos &&
                      builtin.source->find("struct ObjectShaderParameters") != std::string::npos &&
                      builtin.source->find("Matrix4 toy_object_to_world = Matrix4::zero();") != std::string::npos,
                  "builtin header must contain canonical View and Object typed fields");
            check(builtin.source->find(toy3d::sha256_to_hex(builtin_schema.schema_identity)) != std::string::npos &&
                      builtin.source->find(toy3d::sha256_to_hex(calculate_shader_parameter_group_identity(
                          builtin_schema, BindingGroup::View))) != std::string::npos &&
                      builtin.source->find(toy3d::sha256_to_hex(calculate_shader_parameter_group_identity(
                          builtin_schema, BindingGroup::Object))) != std::string::npos,
                  "builtin View/Object metadata must carry identities derived from their canonical schema");
        }
    }

    void test_shader_parameters_writer_incrementality()
    {
        using namespace toy3d;
        using namespace toy3d::shader;
        NativePlatformFile platform_file;
        const PhysicalPath output_directory(TOY3D_SHADER_PARAMETERS_WRITER_TEST_DIR);
        const FileResult<bool> output_exists = platform_file.exists(output_directory);
        if (output_exists.succeeded() && output_exists.value())
            platform_file.remove_directory_tree(output_directory);

        ShaderParametersCodegenResult first_header;
        first_header.output_name = "first.generated.h";
        first_header.source = "first-v1\n";
        ShaderParametersCodegenResult second_header;
        second_header.output_name = "second.generated.h";
        second_header.source = "second-v1\n";
        std::vector<ShaderParametersGeneratedUnit> units = {
            {first_header, {"first.shader"}}, {second_header, {"second.shader"}}};

        const ShaderParametersWriteResult first =
            write_shader_parameter_headers(platform_file, output_directory, units);
        check(first.succeeded() && first.outputs.size() == 2u && first.changed_outputs.size() == 2u,
              "the first generated-header publication must write every output");
        const FileResult<PhysicalPath> first_path =
            platform_file.join_relative(output_directory, "first.generated.h");
        const FileResult<std::string> first_bytes =
            first_path.succeeded() ? platform_file.read_text_utf8(first_path.value())
                                   : FileResult<std::string>(FileStatus{});

        const ShaderParametersWriteResult repeated =
            write_shader_parameter_headers(platform_file, output_directory, units);
        const FileResult<std::string> repeated_bytes =
            first_path.succeeded() ? platform_file.read_text_utf8(first_path.value())
                                   : FileResult<std::string>(FileStatus{});
        check(repeated.succeeded() && repeated.changed_outputs.empty() && first_bytes.succeeded() &&
                  repeated_bytes.succeeded() && first_bytes.value() == repeated_bytes.value(),
              "identical generation must preserve byte-identical outputs without replacement");

        units[0].header.source = "first-v2\n";
        const ShaderParametersWriteResult one_changed =
            write_shader_parameter_headers(platform_file, output_directory, units);
        check(one_changed.succeeded() && one_changed.changed_outputs.size() == 1u &&
                  one_changed.changed_outputs.front() == first_path.value(),
              "a single schema unit change must replace only its generated header");

        units.erase(units.begin() + 1);
        const ShaderParametersWriteResult stale_removed =
            write_shader_parameter_headers(platform_file, output_directory, units);
        const FileResult<PhysicalPath> second_path =
            platform_file.join_relative(output_directory, "second.generated.h");
        const FileResult<bool> second_exists =
            second_path.succeeded() ? platform_file.exists(second_path.value()) : FileResult<bool>(FileStatus{});
        const FileResult<PhysicalPath> dependencies_path =
            platform_file.join_relative(output_directory, "shader_parameters.dependencies");
        const FileResult<std::string> dependencies =
            dependencies_path.succeeded() ? platform_file.read_text_utf8(dependencies_path.value())
                                          : FileResult<std::string>(FileStatus{});
        check(stale_removed.succeeded() && stale_removed.removed_outputs.size() == 1u &&
                  second_exists.succeeded() && !second_exists.value() && dependencies.succeeded() &&
                  dependencies.value() == "first.generated.h\tfirst.shader\n",
              "owned stale headers must be removed and dependency tracking must match retained units");

        const FileResult<std::uintmax_t> removed = platform_file.remove_directory_tree(output_directory);
        check(removed.succeeded(), "writer test output cleanup must succeed");
    }
} // namespace

int main()
{
    test_constant_buffer_data_layout_hash();
    test_sha256_and_parameter_id();
    test_toy_shader_abi_packing();
    test_logical_layout_determinism();
    test_active_layout_allocators_and_codegen();
    test_cpp_identifier_mapping();
    test_shader_parameters_cpp_codegen();
    test_shader_parameters_writer_incrementality();
    if (failure_count != 0)
    {
        std::cerr << failure_count << " Shader layout test assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Shader layout tests passed.\n";
    return 0;
}
