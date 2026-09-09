#include "drivers/rhi/rhi_command_descriptors.h"
#include "drivers/rhi/rhi_pipeline_cache.h"

#include <iostream>
#include <limits>
#include <memory>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::RHIBindingLayoutEntry make_layout_entry(toy3d::ShaderParameterId binding_id,
                                                    toy3d::RHIBindingGroup group,
                                                    std::uint32_t target_binding,
                                                    toy3d::RHIResourceBindingType type,
                                                    toy3d::RHIShaderStageFlags stages)
    {
        toy3d::RHIBindingLayoutEntry entry;
        entry.binding_id = binding_id;
        entry.group = group;
        entry.target_binding = target_binding;
        entry.type = type;
        entry.stages = stages;
        if (type == toy3d::RHIResourceBindingType::UniformBuffer)
        {
            entry.data_size = 64u;
            entry.data_layout_hash[0] = 1u;
            entry.shader_abi_version = 1u;
        }
        return entry;
    }

} // namespace

int main()
{
    toy3d::RHIShaderDesc vertex_shader;
    vertex_shader.bytecode.bytes = {1u};
    vertex_shader.bytecode.target = "spirv";
    vertex_shader.content_hash = {1u, 0u};
    vertex_shader.vertex_inputs.push_back(
        {"POSITION", 0u, 0u, toy3d::RHIShaderVertexInputReflection::ScalarType::Float32, 3u});
    check(static_cast<bool>(toy3d::validate_shader_desc(vertex_shader)),
          "complete vertex-input reflection must validate");

    toy3d::RHIShaderDesc invalid_shader = vertex_shader;
    invalid_shader.stage = toy3d::RHIShaderStage::Pixel;
    check(!toy3d::validate_shader_desc(invalid_shader), "non-vertex shaders must reject vertex-input reflection");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs.push_back(
        {"POSITION", 0u, 1u, toy3d::RHIShaderVertexInputReflection::ScalarType::Float32, 3u});
    check(!toy3d::validate_shader_desc(invalid_shader), "duplicate vertex-input semantics must fail");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs.push_back(
        {"position", 0u, 1u, toy3d::RHIShaderVertexInputReflection::ScalarType::Float32, 3u});
    check(!toy3d::validate_shader_desc(invalid_shader),
          "vertex-input semantic uniqueness must be ASCII case-insensitive");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs.push_back(
        {"NORMAL", 0u, 0u, toy3d::RHIShaderVertexInputReflection::ScalarType::Float32, 3u});
    check(!toy3d::validate_shader_desc(invalid_shader), "duplicate vertex-input locations must fail");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs[0].component_count = 0u;
    check(!toy3d::validate_shader_desc(invalid_shader), "empty vertex-input component shapes must fail");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs[0].location = std::numeric_limits<std::uint32_t>::max();
    check(!toy3d::validate_shader_desc(invalid_shader), "sentinel vertex-input locations must fail");

    toy3d::RHIShaderKey first_shader_key;
    first_shader_key.vertex_inputs = vertex_shader.vertex_inputs;
    toy3d::RHIShaderKey second_shader_key = first_shader_key;
    check(first_shader_key == second_shader_key, "shader cache identity must accept equal vertex-input reflection");
    second_shader_key.vertex_inputs[0].location = 2u;
    check(!(first_shader_key == second_shader_key), "shader cache identity must include vertex-input target location");

    toy3d::RHIShaderDesc pixel_shader;
    pixel_shader.stage = toy3d::RHIShaderStage::Pixel;
    pixel_shader.bytecode.bytes = {2u};
    pixel_shader.bytecode.target = "spirv";
    pixel_shader.content_hash = {2u, 0u};

    toy3d::RHIGraphicsPipelineDesc pipeline;
    pipeline.vertex_shader = std::make_shared<toy3d::RHIShader>(vertex_shader);
    pipeline.pixel_shader = std::make_shared<toy3d::RHIShader>(pixel_shader);
    pipeline.binding_layout = std::make_shared<toy3d::RHIBindingLayout>(toy3d::RHIBindingLayoutDesc{});
    pipeline.vertex_buffers.push_back({0u, 12u, toy3d::RHIVertexInputRate::PerVertex});
    pipeline.vertex_attributes.push_back({0u, 0u, toy3d::PixelFormat::R32G32B32Float, 0u});
    check(static_cast<bool>(toy3d::validate_graphics_pipeline_desc(pipeline)),
          "pipeline vertex layout must match shader location and float3 shape");
    const toy3d::RHIShaderVertexInputReflection& d3d_input_layout_seam =
        pipeline.vertex_shader->desc().vertex_inputs[0];
    check(d3d_input_layout_seam.semantic_name == "POSITION" && d3d_input_layout_seam.semantic_index == 0u &&
              pipeline.vertex_attributes[0].location == d3d_input_layout_seam.location,
          "D3D backends must be able to recover semantic identity through the common location seam");

    toy3d::RHIGraphicsPipelineDesc invalid_pipeline = pipeline;
    invalid_pipeline.vertex_attributes[0].format = toy3d::PixelFormat::R32G32B32A32Float;
    invalid_pipeline.vertex_buffers[0].stride = 16u;
    check(!toy3d::validate_graphics_pipeline_desc(invalid_pipeline),
          "pipeline vertex format must match shader component count");

    invalid_pipeline = pipeline;
    invalid_pipeline.vertex_attributes[0].location = 1u;
    check(!toy3d::validate_graphics_pipeline_desc(invalid_pipeline),
          "pipeline vertex location must exist in shader reflection");

    invalid_pipeline = pipeline;
    invalid_pipeline.vertex_attributes[0].format = toy3d::PixelFormat::BC1UNorm;
    check(!toy3d::validate_graphics_pipeline_desc(invalid_pipeline),
          "formats without a common RHI vertex shape must fail before backend creation");

    invalid_pipeline = pipeline;
    invalid_pipeline.vertex_attributes[0].offset = 4u;
    check(!toy3d::validate_graphics_pipeline_desc(invalid_pipeline),
          "vertex attribute byte range must fit its declared stride");

    invalid_pipeline = pipeline;
    invalid_pipeline.vertex_attributes.clear();
    check(!toy3d::validate_graphics_pipeline_desc(invalid_pipeline),
          "pipeline vertex layout must provide every reflected shader input");

    toy3d::RHIBindingLayoutDesc cross_group_slots;
    cross_group_slots.entries.push_back(make_layout_entry(3u, toy3d::RHIBindingGroup::Global, 0u,
                                                          toy3d::RHIResourceBindingType::SampledTexture,
                                                          toy3d::RHIShaderStageFlags::Pixel));
    cross_group_slots.entries.push_back(make_layout_entry(4u, toy3d::RHIBindingGroup::Material, 0u,
                                                          toy3d::RHIResourceBindingType::SampledTexture,
                                                          toy3d::RHIShaderStageFlags::Pixel));
    check(static_cast<bool>(toy3d::validate_binding_layout_desc(cross_group_slots)),
          "different logical groups may reuse target slots in different native namespaces");

    toy3d::RHIBindingLayoutDesc same_group_overlap = cross_group_slots;
    same_group_overlap.entries.back().group = toy3d::RHIBindingGroup::Global;
    check(!toy3d::validate_binding_layout_desc(same_group_overlap),
          "one logical group must reject overlapping target slots");

    toy3d::RHIGraphicsBindings empty;
    check(static_cast<bool>(toy3d::validate_graphics_bindings(empty)),
          "an empty binding snapshot must be valid before pipeline requirements are known");

    std::uint8_t uniform_byte = 1u;
    toy3d::RHITransientUniformDataDesc transient_uniform;
    transient_uniform.source = {&uniform_byte, sizeof(uniform_byte), 0u, 0u};
    transient_uniform.data_layout_hash[0] = 1u;
    transient_uniform.shader_abi_version = 1u;
    check(static_cast<bool>(toy3d::validate_transient_uniform_data_desc(transient_uniform)),
          "transient uniform data requires copied source bytes and complete data ABI identity");
    toy3d::RHITransientUniformDataDesc invalid_transient = transient_uniform;
    invalid_transient.source = {};
    check(!toy3d::validate_transient_uniform_data_desc(invalid_transient),
          "empty transient uniform source data must fail");
    invalid_transient = transient_uniform;
    invalid_transient.data_layout_hash = {};
    check(!toy3d::validate_transient_uniform_data_desc(invalid_transient),
          "transient uniform data without a layout hash must fail");
    invalid_transient = transient_uniform;
    invalid_transient.shader_abi_version = 0u;
    check(!toy3d::validate_transient_uniform_data_desc(invalid_transient),
          "transient uniform data without a Shader ABI version must fail");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RHI binding tests passed\n";
    return 0;
}
