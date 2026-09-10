#include "drivers/rhi/rhi_command_descriptors.h"
#include "drivers/rhi/rhi_pipeline_cache.h"

#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

    std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment)
    {
        return ((value + alignment - 1u) / alignment) * alignment;
    }

    struct UniformUploadModelResult
    {
        toy3d::RHIUniformBufferSlice slice;
        std::uint64_t allocation_size = 0;
        std::vector<std::uint8_t> copied_bytes;
    };

    UniformUploadModelResult model_vulkan_dynamic_uniform_upload(
        const toy3d::RHITransientUniformDataDesc& desc, const toy3d::RHIBufferRef& arena,
        std::uint64_t& next_offset, std::uint64_t alignment)
    {
        const std::uint64_t offset = align_up(next_offset, alignment);
        next_offset = offset + desc.source.size;
        const auto* source = static_cast<const std::uint8_t*>(desc.source.data);
        return {{arena, offset, desc.source.size}, desc.source.size,
                std::vector<std::uint8_t>(source, source + desc.source.size)};
    }

    UniformUploadModelResult model_d3d12_aligned_suballocation(
        const toy3d::RHITransientUniformDataDesc& desc, const toy3d::RHIBufferRef& arena,
        std::uint64_t& next_offset)
    {
        constexpr std::uint64_t d3d12_constant_buffer_alignment = 256u;
        const std::uint64_t offset = align_up(next_offset, d3d12_constant_buffer_alignment);
        const std::uint64_t allocation_size = align_up(desc.source.size, d3d12_constant_buffer_alignment);
        next_offset = offset + allocation_size;
        const auto* source = static_cast<const std::uint8_t*>(desc.source.data);
        return {{arena, offset, desc.source.size}, allocation_size,
                std::vector<std::uint8_t>(source, source + desc.source.size)};
    }

    UniformUploadModelResult model_d3d11_standalone_constant_buffer(
        const toy3d::RHITransientUniformDataDesc& desc)
    {
        constexpr std::uint64_t d3d11_constant_buffer_alignment = 16u;
        const std::uint64_t allocation_size = align_up(desc.source.size, d3d11_constant_buffer_alignment);
        toy3d::RHIBufferDesc buffer_desc;
        buffer_desc.size = allocation_size;
        buffer_desc.usage = toy3d::RHIResourceUsage::UniformBuffer;
        buffer_desc.debug_name = desc.debug_name;
        const auto* source = static_cast<const std::uint8_t*>(desc.source.data);
        return {{std::make_shared<toy3d::RHIBuffer>(std::move(buffer_desc)), 0u, desc.source.size}, allocation_size,
                std::vector<std::uint8_t>(source, source + desc.source.size)};
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
    check(static_cast<bool>(toy3d::validate_transient_uniform_data_desc(transient_uniform)),
          "transient uniform data accepts copied source bytes without Shader ABI metadata");
    toy3d::RHITransientUniformDataDesc invalid_transient = transient_uniform;
    invalid_transient.source = {};
    check(!toy3d::validate_transient_uniform_data_desc(invalid_transient),
          "empty transient uniform source data must fail");

    const std::vector<std::uint8_t> uniform_bytes(20u, 0x5au);
    toy3d::RHITransientUniformDataDesc cross_backend_upload;
    cross_backend_upload.source = {uniform_bytes.data(), uniform_bytes.size(), 0u, 0u};
    cross_backend_upload.debug_name = "cross-backend uniform bytes";

    toy3d::RHIBufferDesc arena_desc;
    arena_desc.size = 4096u;
    arena_desc.usage = toy3d::RHIResourceUsage::UniformBuffer;
    const auto shared_arena = std::make_shared<toy3d::RHIBuffer>(arena_desc);
    std::uint64_t vulkan_next_offset = 17u;
    std::uint64_t d3d12_next_offset = 17u;
    const UniformUploadModelResult vulkan_upload =
        model_vulkan_dynamic_uniform_upload(cross_backend_upload, shared_arena, vulkan_next_offset, 64u);
    const UniformUploadModelResult d3d12_upload =
        model_d3d12_aligned_suballocation(cross_backend_upload, shared_arena, d3d12_next_offset);
    const UniformUploadModelResult d3d11_upload =
        model_d3d11_standalone_constant_buffer(cross_backend_upload);
    const UniformUploadModelResult second_d3d11_upload =
        model_d3d11_standalone_constant_buffer(cross_backend_upload);

    check(vulkan_upload.slice.buffer == shared_arena && vulkan_upload.slice.offset == 64u &&
              vulkan_upload.slice.size == uniform_bytes.size() && vulkan_upload.copied_bytes == uniform_bytes,
          "the pure-memory upload contract must support a Vulkan dynamic-uniform arena and logical slice");
    check(d3d12_upload.slice.buffer == shared_arena && d3d12_upload.slice.offset == 256u &&
              d3d12_upload.slice.size == uniform_bytes.size() && d3d12_upload.allocation_size == 256u &&
              d3d12_upload.copied_bytes == uniform_bytes,
          "the same upload contract must support D3D12 256-byte suballocation without exposing its padding");
    check(d3d11_upload.slice.buffer && d3d11_upload.slice.offset == 0u &&
              d3d11_upload.slice.size == uniform_bytes.size() && d3d11_upload.allocation_size == 32u &&
              d3d11_upload.slice.buffer != second_d3d11_upload.slice.buffer &&
              d3d11_upload.copied_bytes == uniform_bytes,
          "the same upload contract must support a D3D11 standalone constant-buffer fallback");

    // C++17 structured binding makes this a compile-time field-count contract: adding native
    // descriptor/register/root fields to either public aggregate breaks this backend-neutral test.
    const auto [public_source, public_debug_name] = cross_backend_upload;
    const auto [public_buffer, public_offset, public_size] = d3d12_upload.slice;
    check(public_source.data == uniform_bytes.data() && public_debug_name == cross_backend_upload.debug_name &&
              public_buffer == shared_arena && public_offset == 256u && public_size == uniform_bytes.size(),
          "public transient uniform aggregates must contain only source/debug and buffer/offset/size semantics");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RHI binding tests passed\n";
    return 0;
}
