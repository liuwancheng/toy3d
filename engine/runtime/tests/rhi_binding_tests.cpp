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

    toy3d::RHIBindingLayoutRef make_layout(bool include_view)
    {
        toy3d::RHIBindingLayoutDesc desc;
        desc.entries.push_back({toy3d::RHIBindingGroup::Global, 0,
            toy3d::RHIResourceBindingType::UniformBuffer,
            toy3d::RHIShaderStageFlags::Vertex, 1});
        if (include_view)
        {
            desc.entries.push_back({toy3d::RHIBindingGroup::View, 1,
                toy3d::RHIResourceBindingType::UniformBuffer,
                toy3d::RHIShaderStageFlags::Vertex, 1});
        }
        return std::make_shared<toy3d::RHIBindingLayout>(std::move(desc));
    }

    toy3d::RHIBindingSetRef make_set(
        const toy3d::RHIBindingLayoutRef& layout,
        toy3d::RHIBindingGroup group)
    {
        toy3d::RHIBindingSetDesc desc;
        desc.layout = layout;
        desc.group = group;
        return std::make_shared<toy3d::RHIBindingSet>(std::move(desc));
    }
}

int main()
{
    toy3d::RHIShaderDesc vertex_shader;
    vertex_shader.bytecode.bytes = {1u};
    vertex_shader.bytecode.target = "spirv";
    vertex_shader.content_hash = {1u, 0u};
    vertex_shader.vertex_inputs.push_back({"POSITION", 0u, 0u,
        toy3d::RHIShaderVertexInputReflection::ScalarType::Float32, 3u});
    check(static_cast<bool>(toy3d::validate_shader_desc(vertex_shader)),
        "complete vertex-input reflection must validate");

    toy3d::RHIShaderDesc invalid_shader = vertex_shader;
    invalid_shader.stage = toy3d::RHIShaderStage::Pixel;
    check(!toy3d::validate_shader_desc(invalid_shader),
        "non-vertex shaders must reject vertex-input reflection");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs.push_back({"POSITION", 0u, 1u,
        toy3d::RHIShaderVertexInputReflection::ScalarType::Float32, 3u});
    check(!toy3d::validate_shader_desc(invalid_shader),
        "duplicate vertex-input semantics must fail");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs.push_back({"position", 0u, 1u,
        toy3d::RHIShaderVertexInputReflection::ScalarType::Float32, 3u});
    check(!toy3d::validate_shader_desc(invalid_shader),
        "vertex-input semantic uniqueness must be ASCII case-insensitive");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs.push_back({"NORMAL", 0u, 0u,
        toy3d::RHIShaderVertexInputReflection::ScalarType::Float32, 3u});
    check(!toy3d::validate_shader_desc(invalid_shader),
        "duplicate vertex-input locations must fail");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs[0].component_count = 0u;
    check(!toy3d::validate_shader_desc(invalid_shader),
        "empty vertex-input component shapes must fail");

    invalid_shader = vertex_shader;
    invalid_shader.vertex_inputs[0].location =
        std::numeric_limits<std::uint32_t>::max();
    check(!toy3d::validate_shader_desc(invalid_shader),
        "sentinel vertex-input locations must fail");

    toy3d::RHIShaderKey first_shader_key;
    first_shader_key.vertex_inputs = vertex_shader.vertex_inputs;
    toy3d::RHIShaderKey second_shader_key = first_shader_key;
    check(first_shader_key == second_shader_key,
        "shader cache identity must accept equal vertex-input reflection");
    second_shader_key.vertex_inputs[0].location = 2u;
    check(!(first_shader_key == second_shader_key),
        "shader cache identity must include vertex-input target location");

    toy3d::RHIShaderDesc pixel_shader;
    pixel_shader.stage = toy3d::RHIShaderStage::Pixel;
    pixel_shader.bytecode.bytes = {2u};
    pixel_shader.bytecode.target = "spirv";
    pixel_shader.content_hash = {2u, 0u};

    toy3d::RHIGraphicsPipelineDesc pipeline;
    pipeline.vertex_shader = std::make_shared<toy3d::RHIShader>(vertex_shader);
    pipeline.pixel_shader = std::make_shared<toy3d::RHIShader>(pixel_shader);
    pipeline.binding_layout = std::make_shared<toy3d::RHIBindingLayout>(
        toy3d::RHIBindingLayoutDesc{});
    pipeline.vertex_buffers.push_back({0u, 12u,
        toy3d::RHIVertexInputRate::PerVertex});
    pipeline.vertex_attributes.push_back(
        {0u, 0u, toy3d::PixelFormat::R32G32B32Float, 0u});
    check(static_cast<bool>(toy3d::validate_graphics_pipeline_desc(pipeline)),
        "pipeline vertex layout must match shader location and float3 shape");
    const toy3d::RHIShaderVertexInputReflection& d3d_input_layout_seam =
        pipeline.vertex_shader->desc().vertex_inputs[0];
    check(d3d_input_layout_seam.semantic_name == "POSITION" &&
          d3d_input_layout_seam.semantic_index == 0u &&
          pipeline.vertex_attributes[0].location == d3d_input_layout_seam.location,
        "D3D backends must be able to recover semantic identity through the common location seam");

    toy3d::RHIGraphicsPipelineDesc invalid_pipeline = pipeline;
    invalid_pipeline.vertex_attributes[0].format =
        toy3d::PixelFormat::R32G32B32A32Float;
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
    cross_group_slots.entries.push_back({toy3d::RHIBindingGroup::Global, 0,
        toy3d::RHIResourceBindingType::SampledTexture,
        toy3d::RHIShaderStageFlags::Pixel, 1});
    cross_group_slots.entries.push_back({toy3d::RHIBindingGroup::Material, 0,
        toy3d::RHIResourceBindingType::SampledTexture,
        toy3d::RHIShaderStageFlags::Pixel, 1});
    check(static_cast<bool>(toy3d::validate_binding_layout_desc(cross_group_slots)),
        "different logical groups may reuse target slots in different native namespaces");

    toy3d::RHIBindingLayoutDesc same_group_overlap = cross_group_slots;
    same_group_overlap.entries.back().group = toy3d::RHIBindingGroup::Global;
    check(!toy3d::validate_binding_layout_desc(same_group_overlap),
        "one logical group must reject overlapping target slots");

    const toy3d::RHIBindingLayoutRef layout = make_layout(true);
    const toy3d::RHIBindingSetRef global = make_set(
        layout, toy3d::RHIBindingGroup::Global);
    const toy3d::RHIBindingSetRef view = make_set(
        layout, toy3d::RHIBindingGroup::View);

    toy3d::RHIGraphicsBindings valid;
    valid.global = global;
    valid.view = view;
    check(static_cast<bool>(toy3d::validate_graphics_bindings(valid)),
        "Global and View sets with one compatible layout must validate");

    toy3d::RHIGraphicsBindings wrong_field;
    wrong_field.view = global;
    check(!toy3d::validate_graphics_bindings(wrong_field),
        "a logical binding set in the wrong field must fail");

    toy3d::RHIGraphicsBindings incompatible;
    incompatible.global = global;
    incompatible.view = make_set(make_layout(false), toy3d::RHIBindingGroup::View);
    check(!toy3d::validate_graphics_bindings(incompatible),
        "binding sets with incompatible layouts must fail");

    toy3d::RHIGraphicsBindings empty;
    check(static_cast<bool>(toy3d::validate_graphics_bindings(empty)),
        "an empty binding snapshot must be valid before pipeline requirements are known");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RHI binding tests passed\n";
    return 0;
}
