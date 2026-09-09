#include "codegen/binding_codegen.h"
#include "format/sha256.h"
#include "frontend/shader_parser.h"
#include "layout/binding_allocator.h"
#include "layout/shader_layout.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
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
        const toy3d::shader::Sha256Hash hash = toy3d::shader::sha256("abc");
        const toy3d::shader::Sha256Hash expected = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
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
            check(first.layout->logical_layout_hash != reordered_constant_layout.layout->logical_layout_hash,
                  "constant member source order must change offsets and logical layout hash");
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
} // namespace

int main()
{
    test_constant_buffer_data_layout_hash();
    test_sha256_and_parameter_id();
    test_toy_shader_abi_packing();
    test_logical_layout_determinism();
    test_active_layout_allocators_and_codegen();
    if (failure_count != 0)
    {
        std::cerr << failure_count << " Shader layout test assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Shader layout tests passed.\n";
    return 0;
}
