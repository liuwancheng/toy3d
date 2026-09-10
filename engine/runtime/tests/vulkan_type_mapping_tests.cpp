#include "drivers/vulkan/vulkan_binding_creation.h"
#include "drivers/vulkan/vulkan_device.h"
#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_type_mapping.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
} // namespace

int main()
{
    using namespace toy3d;

    require(vulkan_format_from_pixel_format(PixelFormat::R16G16B16A16Float) == VK_FORMAT_R16G16B16A16_SFLOAT,
            "RGBA16F must retain its Vulkan mapping.");
    require(vulkan_format_from_pixel_format(PixelFormat::D24UNormS8UInt) == VK_FORMAT_D24_UNORM_S8_UINT,
            "D24S8 must retain its Vulkan mapping.");
    require(vulkan_format_from_pixel_format(PixelFormat::Unknown) == VK_FORMAT_UNDEFINED,
            "Unknown pixel formats must map to VK_FORMAT_UNDEFINED.");
    require(is_vk_depth_format(VK_FORMAT_D32_SFLOAT) && !is_vk_stencil_format(VK_FORMAT_D32_SFLOAT) &&
                is_vk_stencil_format(VK_FORMAT_D24_UNORM_S8_UINT),
            "Depth and stencil classification must remain distinct.");

    const auto topology = to_vk_primitive_topology(RHIPrimitiveTopology::TriangleList);
    require(topology && topology.value() == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            "Triangle-list topology must retain its Vulkan mapping.");
    const auto invalid_topology = to_vk_primitive_topology(static_cast<RHIPrimitiveTopology>(255));
    require(!invalid_topology && invalid_topology.status().code() == RHIErrorCode::InvalidArgument,
            "Unknown topology must fail with InvalidArgument.");

    const auto counter_clockwise = to_vk_front_face(RHIFrontFace::CounterClockwise);
    const auto clockwise = to_vk_front_face(RHIFrontFace::Clockwise);
    require(counter_clockwise && counter_clockwise.value() == VK_FRONT_FACE_CLOCKWISE && clockwise &&
                clockwise.value() == VK_FRONT_FACE_COUNTER_CLOCKWISE,
            "Negative-height Vulkan viewports must reverse native front-face winding.");

    RHIViewport public_viewport;
    public_viewport.x = 13.0F;
    public_viewport.y = 17.0F;
    public_viewport.width = 640.0F;
    public_viewport.height = 360.0F;
    public_viewport.min_depth = 0.25F;
    public_viewport.max_depth = 0.75F;
    const VkViewport native_viewport = to_vk_viewport(public_viewport);
    require(native_viewport.x == 13.0F && native_viewport.y == 377.0F && native_viewport.width == 640.0F &&
                native_viewport.height == -360.0F && native_viewport.minDepth == 0.25F &&
                native_viewport.maxDepth == 0.75F,
            "Vulkan viewports must apply the backend-owned negative-height Y transform.");

    const auto unsupported_samples = to_vk_sample_count(3);
    require(!unsupported_samples && unsupported_samples.status().code() == RHIErrorCode::Unsupported,
            "Unsupported sample counts must remain diagnostic.");
    const auto depth_aspect = to_vk_image_aspect(RHITextureAspect::Depth, VK_FORMAT_D24_UNORM_S8_UINT);
    require(depth_aspect && depth_aspect.value() == VK_IMAGE_ASPECT_DEPTH_BIT,
            "Depth-only views of packed depth-stencil formats must remain supported.");
    const auto invalid_color_aspect = to_vk_image_aspect(RHITextureAspect::Color, VK_FORMAT_D32_SFLOAT);
    require(!invalid_color_aspect && invalid_color_aspect.status().code() == RHIErrorCode::InvalidArgument,
            "Incompatible image aspects must fail with InvalidArgument.");

    require(to_vk_descriptor_type(RHIResourceBindingType::UniformBuffer) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
                to_vk_descriptor_type(RHIResourceBindingType::SampledTexture) == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            "Binding resource classes must retain their Vulkan descriptor mappings.");
    require(to_vk_color_write_mask(RHIColorWriteMask::Red | RHIColorWriteMask::Alpha) ==
                (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_A_BIT),
            "Color write masks must preserve individual channel bits.");

    require(VulkanBindingLayout::physical_set(RHIBindingGroup::Global) == 0u &&
                VulkanBindingLayout::physical_set(RHIBindingGroup::View) == 0u &&
                VulkanBindingLayout::physical_set(RHIBindingGroup::Pass) == 1u &&
                VulkanBindingLayout::physical_set(RHIBindingGroup::Material) == 2u &&
                VulkanBindingLayout::physical_set(RHIBindingGroup::Object) == 3u,
            "Five logical groups must retain the Vulkan ES3.1 four-set aggregation.");

    VulkanDevice device;
    RHIBufferDesc uniform_buffer_desc;
    uniform_buffer_desc.size = 1024u;
    uniform_buffer_desc.usage = RHIResourceUsage::UniformBuffer;
    const auto uniform_buffer = std::make_shared<VulkanBuffer>(
        device, uniform_buffer_desc, std::shared_ptr<VulkanUploadPage>{}, RHIAccess::UniformBuffer);

    const auto make_resolved_uniform = [&](ShaderParameterId binding_id, RHIBindingGroup group,
                                           std::uint32_t target_binding, std::uint64_t offset)
    {
        rhi_detail::ResolvedBinding resolved;
        resolved.layout.binding_id = binding_id;
        resolved.layout.group = group;
        resolved.layout.target_binding = target_binding;
        resolved.layout.type = RHIResourceBindingType::UniformBuffer;
        resolved.layout.stages = RHIShaderStageFlags::Vertex;
        resolved.layout.data_size = 64u;
        resolved.value.binding_id = binding_id;
        resolved.value.buffer = uniform_buffer;
        resolved.value.buffer_offset = offset;
        resolved.value.buffer_size = 64u;
        return resolved;
    };

    std::vector<rhi_detail::ResolvedBinding> resolved_bindings;
    resolved_bindings.push_back(make_resolved_uniform(2u, RHIBindingGroup::View, 1u, 512u));
    resolved_bindings.push_back(make_resolved_uniform(1u, RHIBindingGroup::Global, 0u, 256u));
    resolved_bindings.push_back(make_resolved_uniform(3u, RHIBindingGroup::Pass, 0u, 0u));
    resolved_bindings.push_back(make_resolved_uniform(4u, RHIBindingGroup::Material, 0u, 0u));
    resolved_bindings.push_back(make_resolved_uniform(5u, RHIBindingGroup::Object, 0u, 0u));

    const VulkanPhysicalBindingSources physical_sources =
        make_vulkan_physical_binding_sources(resolved_bindings);
    require(physical_sources[0].size() == 2u && physical_sources[0][0].layout.group == RHIBindingGroup::Global &&
                physical_sources[0][1].layout.group == RHIBindingGroup::View &&
                physical_sources[1].size() == 1u && physical_sources[2].size() == 1u &&
                physical_sources[3].size() == 1u,
            "Vulkan binding planning must aggregate Global and View atomically and populate four physical sets.");

    RHIBindingLayoutDesc binding_layout_desc;
    for (const rhi_detail::ResolvedBinding& resolved : resolved_bindings)
    {
        binding_layout_desc.entries.push_back(resolved.layout);
    }
    VulkanBindingLayout binding_layout(
        device, std::move(binding_layout_desc), VK_NULL_HANDLE,
        std::array<VkDescriptorSetLayout, VulkanBindingLayout::physical_set_count>{});
    const std::string first_packet_key =
        make_vulkan_binding_packet_cache_key(binding_layout, 0u, physical_sources[0]);
    std::vector<rhi_detail::ResolvedBinding> moved_uniforms = physical_sources[0];
    moved_uniforms[0].value.buffer_offset = 768u;
    moved_uniforms[1].value.buffer_offset = 896u;
    const std::string moved_packet_key =
        make_vulkan_binding_packet_cache_key(binding_layout, 0u, moved_uniforms);
    require(first_packet_key == moved_packet_key,
            "Dynamic uniform offsets must not invalidate the recording-local Vulkan packet cache.");

    const auto first_offsets = collect_vulkan_dynamic_uniform_offsets(physical_sources[0]);
    const auto moved_offsets = collect_vulkan_dynamic_uniform_offsets(moved_uniforms);
    require(first_offsets && moved_offsets && first_offsets.value() == std::vector<std::uint32_t>({256u, 512u}) &&
                moved_offsets.value() == std::vector<std::uint32_t>({768u, 896u}),
            "Vulkan dynamic uniform offsets must follow physical binding order independently of packet identity.");
    moved_uniforms[0].value.buffer_offset =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1u;
    const auto overflowing_offsets = collect_vulkan_dynamic_uniform_offsets(moved_uniforms);
    require(!overflowing_offsets && overflowing_offsets.status().code() == RHIErrorCode::Unsupported,
            "Vulkan dynamic uniform offsets outside the native 32-bit range must remain diagnostic.");

    VulkanAccessState access_state;
    require(get_vulkan_access_state(RHIAccess::CopyDestination, access_state) &&
                access_state.pipeline_stage == VK_PIPELINE_STAGE_TRANSFER_BIT &&
                access_state.access_mask == VK_ACCESS_TRANSFER_WRITE_BIT && access_state.supports_buffer &&
                access_state.supports_image,
            "Copy-destination access must retain its transfer mapping.");
    require(!get_vulkan_access_state(RHIAccess::Unknown, access_state) &&
                get_vulkan_access_state(RHIAccess::Unknown, access_state).code() == RHIErrorCode::Unsupported,
            "Unknown access must remain Unsupported.");

    return EXIT_SUCCESS;
}
