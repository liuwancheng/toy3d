#include "drivers/vulkan/vulkan_type_mapping.h"

#include <cstdlib>
#include <iostream>

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
}

int main()
{
    using namespace toy3d;

    require(vulkan_format_from_pixel_format(PixelFormat::R16G16B16A16Float) == VK_FORMAT_R16G16B16A16_SFLOAT,
        "RGBA16F must retain its Vulkan mapping.");
    require(vulkan_format_from_pixel_format(PixelFormat::D24UNormS8UInt) == VK_FORMAT_D24_UNORM_S8_UINT,
        "D24S8 must retain its Vulkan mapping.");
    require(vulkan_format_from_pixel_format(PixelFormat::Unknown) == VK_FORMAT_UNDEFINED,
        "Unknown pixel formats must map to VK_FORMAT_UNDEFINED.");
    require(is_vk_depth_format(VK_FORMAT_D32_SFLOAT) &&
            !is_vk_stencil_format(VK_FORMAT_D32_SFLOAT) &&
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
    require(counter_clockwise && counter_clockwise.value() == VK_FRONT_FACE_CLOCKWISE &&
            clockwise && clockwise.value() == VK_FRONT_FACE_COUNTER_CLOCKWISE,
        "Negative-height Vulkan viewports must reverse native front-face winding.");

    RHIViewport public_viewport;
    public_viewport.x = 13.0F;
    public_viewport.y = 17.0F;
    public_viewport.width = 640.0F;
    public_viewport.height = 360.0F;
    public_viewport.min_depth = 0.25F;
    public_viewport.max_depth = 0.75F;
    const VkViewport native_viewport = to_vk_viewport(public_viewport);
    require(native_viewport.x == 13.0F && native_viewport.y == 377.0F &&
            native_viewport.width == 640.0F && native_viewport.height == -360.0F &&
            native_viewport.minDepth == 0.25F && native_viewport.maxDepth == 0.75F,
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

    VulkanAccessState access_state;
    require(get_vulkan_access_state(RHIAccess::CopyDestination, access_state) &&
            access_state.pipeline_stage == VK_PIPELINE_STAGE_TRANSFER_BIT &&
            access_state.access_mask == VK_ACCESS_TRANSFER_WRITE_BIT &&
            access_state.supports_buffer && access_state.supports_image,
        "Copy-destination access must retain its transfer mapping.");
    require(!get_vulkan_access_state(RHIAccess::Unknown, access_state) &&
            get_vulkan_access_state(RHIAccess::Unknown, access_state).code() == RHIErrorCode::Unsupported,
        "Unknown access must remain Unsupported.");

    return EXIT_SUCCESS;
}
