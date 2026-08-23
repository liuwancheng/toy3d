#include "drivers/vulkan/vulkan_device.h"
#include "drivers/vulkan/vulkan_command_context.h"
#include "drivers/vulkan/vulkan_deferred_deletion.h"
#include "drivers/vulkan/vulkan_memory_manager.h"
#include "drivers/vulkan/vulkan_upload_manager.h"
#include "drivers/vulkan/vulkan_queue.h"
#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_viewport_context.h"

#include "drivers/rhi/rhi_queue.h"
#include "logging/logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#if WITH_MAC
#include <GLFW/glfw3.h>
#endif

namespace toy3d
{
    namespace
    {
#if WITH_MAC
        constexpr const char* portability_enumeration_extension_name = "VK_KHR_portability_enumeration";
        constexpr const char* portability_subset_extension_name = "VK_KHR_portability_subset";
        constexpr VkInstanceCreateFlags enumerate_portability_flag = 0x00000001;
#endif

        VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
            VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
            VkDebugUtilsMessageTypeFlagsEXT message_types,
            const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
            void*)
        {
            const char* message = callback_data && callback_data->pMessage
                ? callback_data->pMessage
                : "Vulkan validation emitted an empty message.";
            const char* message_id = callback_data && callback_data->pMessageIdName
                ? callback_data->pMessageIdName
                : "unknown";

            try
            {
                if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
                {
                    TOY_LOG_ERROR("Vulkan validation [{}] (types=0x{:x}): {}", message_id, message_types, message);
                }
                else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
                {
                    TOY_LOG_WARN("Vulkan validation [{}] (types=0x{:x}): {}", message_id, message_types, message);
                }
                else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
                {
                    TOY_LOG_DEBUG("Vulkan validation [{}] (types=0x{:x}): {}", message_id, message_types, message);
                }
                else
                {
                    TOY_LOG_TRACE("Vulkan validation [{}] (types=0x{:x}): {}", message_id, message_types, message);
                }
            }
            catch (...)
            {
                // Exceptions must never cross the Vulkan C callback boundary.
            }
            return VK_FALSE;
        }

        VkDebugUtilsMessengerCreateInfoEXT make_debug_messenger_create_info()
        {
            VkDebugUtilsMessengerCreateInfoEXT create_info{
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            create_info.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            create_info.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            create_info.pfnUserCallback = vulkan_debug_callback;
            return create_info;
        }

        RHIStatus configure_vulkan_environment(bool enable_validation)
        {
#if WITH_WIN64
            if (!enable_validation || GetEnvironmentVariableA("VK_LAYER_PATH", nullptr, 0) != 0)
            {
                return RHIStatus::success();
            }
            if (GetLastError() != ERROR_ENVVAR_NOT_FOUND)
            {
                return RHIStatus::failure(
                    RHIErrorCode::BackendFailure,
                    "Failed to query the VK_LAYER_PATH environment variable.");
            }
            if (!SetEnvironmentVariableA("VK_LAYER_PATH", TOY3D_VK_LAYER_PATH))
            {
                return RHIStatus::failure(
                    RHIErrorCode::BackendFailure,
                    "Failed to configure VK_LAYER_PATH for the bundled validation layer.");
            }
#elif WITH_MAC
            if (std::getenv("VK_ICD_FILENAMES") == nullptr &&
                setenv("VK_ICD_FILENAMES", TOY3D_VK_ICD_PATH, 0) != 0)
            {
                return RHIStatus::failure(
                    RHIErrorCode::BackendFailure,
                    "Failed to configure VK_ICD_FILENAMES for the bundled MoltenVK ICD.");
            }
            if (enable_validation && std::getenv("VK_LAYER_PATH") == nullptr &&
                setenv("VK_LAYER_PATH", TOY3D_VK_LAYER_PATH, 0) != 0)
            {
                return RHIStatus::failure(
                    RHIErrorCode::BackendFailure,
                    "Failed to configure VK_LAYER_PATH for the bundled validation layer.");
            }
#endif
            return RHIStatus::success();
        }

        RHIStatus make_vulkan_status(VkResult result, const char* operation)
        {
            if (result == VK_SUCCESS)
            {
                return RHIStatus::success();
            }
            RHIErrorCode code = RHIErrorCode::BackendFailure;
            if (result == VK_ERROR_DEVICE_LOST)
            {
                code = RHIErrorCode::DeviceLost;
            }
            else if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            {
                code = RHIErrorCode::OutOfMemory;
            }
            else if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                code = RHIErrorCode::OutOfDate;
            }
            return RHIStatus::failure(
                code,
                std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result)) + ".");
        }

        bool has_instance_extension(
            const std::vector<VkExtensionProperties>& extensions,
            const char* name)
        {
            return std::any_of(
                extensions.begin(),
                extensions.end(),
                [name](const VkExtensionProperties& extension)
                {
                    return std::strcmp(extension.extensionName, name) == 0;
                });
        }

        bool has_device_extension(
            VkPhysicalDevice physical_device,
            const char* name)
        {
            std::uint32_t extension_count = 0;
            if (vkEnumerateDeviceExtensionProperties(
                    physical_device,
                    nullptr,
                    &extension_count,
                    nullptr) != VK_SUCCESS)
            {
                return false;
            }

            std::vector<VkExtensionProperties> extensions(extension_count);
            if (vkEnumerateDeviceExtensionProperties(
                    physical_device,
                    nullptr,
                    &extension_count,
                    extensions.data()) != VK_SUCCESS)
            {
                return false;
            }

            return has_instance_extension(extensions, name);
        }

        VkFormat to_vk_format(RHIFormat format)
        {
            switch (format)
            {
            case RHIFormat::R8UNorm:
                return VK_FORMAT_R8_UNORM;
            case RHIFormat::R8G8B8A8UNorm:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case RHIFormat::R8G8B8A8UNormSRGB:
                return VK_FORMAT_R8G8B8A8_SRGB;
            case RHIFormat::B8G8R8A8UNorm:
                return VK_FORMAT_B8G8R8A8_UNORM;
            case RHIFormat::B8G8R8A8UNormSRGB:
                return VK_FORMAT_B8G8R8A8_SRGB;
            case RHIFormat::R16Float:
                return VK_FORMAT_R16_SFLOAT;
            case RHIFormat::R16G16Float:
                return VK_FORMAT_R16G16_SFLOAT;
            case RHIFormat::R16G16B16A16Float:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            case RHIFormat::R32Float:
                return VK_FORMAT_R32_SFLOAT;
            case RHIFormat::R32G32Float:
                return VK_FORMAT_R32G32_SFLOAT;
            case RHIFormat::R32G32B32Float:
                return VK_FORMAT_R32G32B32_SFLOAT;
            case RHIFormat::R32G32B32A32Float:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case RHIFormat::R16UInt:
                return VK_FORMAT_R16_UINT;
            case RHIFormat::R32UInt:
                return VK_FORMAT_R32_UINT;
            case RHIFormat::D16UNorm:
                return VK_FORMAT_D16_UNORM;
            case RHIFormat::D24UNormS8UInt:
                return VK_FORMAT_D24_UNORM_S8_UINT;
            case RHIFormat::D32Float:
                return VK_FORMAT_D32_SFLOAT;
            case RHIFormat::D32FloatS8UInt:
                return VK_FORMAT_D32_SFLOAT_S8_UINT;
            default:
                return VK_FORMAT_UNDEFINED;
            }
        }

        RHIResult<VkPrimitiveTopology> to_vk_primitive_topology(RHIPrimitiveTopology topology)
        {
            switch (topology)
            {
            case RHIPrimitiveTopology::PointList:
                return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
            case RHIPrimitiveTopology::LineList:
                return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
            case RHIPrimitiveTopology::LineStrip:
                return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
            case RHIPrimitiveTopology::TriangleList:
                return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            case RHIPrimitiveTopology::TriangleStrip:
                return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
            }
            return RHIResult<VkPrimitiveTopology>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI primitive topology.");
        }

        RHIResult<VkVertexInputRate> to_vk_vertex_input_rate(RHIVertexInputRate input_rate)
        {
            switch (input_rate)
            {
            case RHIVertexInputRate::PerVertex:
                return RHIResult<VkVertexInputRate>::success(VK_VERTEX_INPUT_RATE_VERTEX);
            case RHIVertexInputRate::PerInstance:
                return RHIResult<VkVertexInputRate>::success(VK_VERTEX_INPUT_RATE_INSTANCE);
            }
            return RHIResult<VkVertexInputRate>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI vertex input rate.");
        }

        RHIResult<VkCullModeFlags> to_vk_cull_mode(RHICullMode cull_mode)
        {
            switch (cull_mode)
            {
            case RHICullMode::None:
                return RHIResult<VkCullModeFlags>::success(VK_CULL_MODE_NONE);
            case RHICullMode::Front:
                return RHIResult<VkCullModeFlags>::success(VK_CULL_MODE_FRONT_BIT);
            case RHICullMode::Back:
                return RHIResult<VkCullModeFlags>::success(VK_CULL_MODE_BACK_BIT);
            }
            return RHIResult<VkCullModeFlags>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI cull mode.");
        }

        RHIResult<VkFrontFace> to_vk_front_face(RHIFrontFace front_face)
        {
            switch (front_face)
            {
            case RHIFrontFace::CounterClockwise:
                return RHIResult<VkFrontFace>::success(VK_FRONT_FACE_COUNTER_CLOCKWISE);
            case RHIFrontFace::Clockwise:
                return RHIResult<VkFrontFace>::success(VK_FRONT_FACE_CLOCKWISE);
            }
            return RHIResult<VkFrontFace>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI front-face winding.");
        }

        RHIResult<VkBlendFactor> to_vk_blend_factor(RHIBlendFactor factor)
        {
            switch (factor)
            {
            case RHIBlendFactor::Zero: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ZERO);
            case RHIBlendFactor::One: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE);
            case RHIBlendFactor::SourceColor: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_SRC_COLOR);
            case RHIBlendFactor::OneMinusSourceColor: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR);
            case RHIBlendFactor::DestinationColor: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_DST_COLOR);
            case RHIBlendFactor::OneMinusDestinationColor: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR);
            case RHIBlendFactor::SourceAlpha: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_SRC_ALPHA);
            case RHIBlendFactor::OneMinusSourceAlpha: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
            case RHIBlendFactor::DestinationAlpha: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_DST_ALPHA);
            case RHIBlendFactor::OneMinusDestinationAlpha: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA);
            case RHIBlendFactor::ConstantColor: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_CONSTANT_COLOR);
            case RHIBlendFactor::OneMinusConstantColor: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR);
            case RHIBlendFactor::SourceAlphaSaturate: return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_SRC_ALPHA_SATURATE);
            }
            return RHIResult<VkBlendFactor>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI blend factor.");
        }

        RHIResult<VkBlendOp> to_vk_blend_operation(RHIBlendOperation operation)
        {
            switch (operation)
            {
            case RHIBlendOperation::Add: return RHIResult<VkBlendOp>::success(VK_BLEND_OP_ADD);
            case RHIBlendOperation::Subtract: return RHIResult<VkBlendOp>::success(VK_BLEND_OP_SUBTRACT);
            case RHIBlendOperation::ReverseSubtract: return RHIResult<VkBlendOp>::success(VK_BLEND_OP_REVERSE_SUBTRACT);
            case RHIBlendOperation::Min: return RHIResult<VkBlendOp>::success(VK_BLEND_OP_MIN);
            case RHIBlendOperation::Max: return RHIResult<VkBlendOp>::success(VK_BLEND_OP_MAX);
            }
            return RHIResult<VkBlendOp>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI blend operation.");
        }

        VkColorComponentFlags to_vk_color_write_mask(RHIColorWriteMask mask)
        {
            VkColorComponentFlags result = 0;
            if (rhi_has_any_flag(mask, RHIColorWriteMask::Red)) result |= VK_COLOR_COMPONENT_R_BIT;
            if (rhi_has_any_flag(mask, RHIColorWriteMask::Green)) result |= VK_COLOR_COMPONENT_G_BIT;
            if (rhi_has_any_flag(mask, RHIColorWriteMask::Blue)) result |= VK_COLOR_COMPONENT_B_BIT;
            if (rhi_has_any_flag(mask, RHIColorWriteMask::Alpha)) result |= VK_COLOR_COMPONENT_A_BIT;
            return result;
        }

        VkShaderStageFlags to_vk_shader_stage_flags(RHIShaderStageFlags stages)
        {
            VkShaderStageFlags result = 0;
            if (rhi_has_any_flag(stages, RHIShaderStageFlags::Vertex)) result |= VK_SHADER_STAGE_VERTEX_BIT;
            if (rhi_has_any_flag(stages, RHIShaderStageFlags::Pixel)) result |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (rhi_has_any_flag(stages, RHIShaderStageFlags::Geometry)) result |= VK_SHADER_STAGE_GEOMETRY_BIT;
            if (rhi_has_any_flag(stages, RHIShaderStageFlags::Hull)) result |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            if (rhi_has_any_flag(stages, RHIShaderStageFlags::Domain)) result |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            if (rhi_has_any_flag(stages, RHIShaderStageFlags::Compute)) result |= VK_SHADER_STAGE_COMPUTE_BIT;
            return result;
        }

        VkDescriptorType to_vk_descriptor_type(RHIResourceBindingType type)
        {
            switch (type)
            {
            case RHIResourceBindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case RHIResourceBindingType::SampledTexture: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case RHIResourceBindingType::StorageTexture: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case RHIResourceBindingType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
            case RHIResourceBindingType::ReadOnlyBuffer:
            case RHIResourceBindingType::StorageBuffer:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }
            return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }

        VkFilter to_vk_filter(RHIFilter filter)
        {
            return filter == RHIFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        }

        VkSamplerMipmapMode to_vk_mipmap_mode(RHIFilter filter)
        {
            return filter == RHIFilter::Nearest
                ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                : VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }

        VkSamplerAddressMode to_vk_address_mode(RHIAddressMode mode)
        {
            switch (mode)
            {
            case RHIAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case RHIAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case RHIAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case RHIAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            }
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }

        VkCompareOp to_vk_compare_operation(RHICompareOperation operation)
        {
            switch (operation)
            {
            case RHICompareOperation::Never: return VK_COMPARE_OP_NEVER;
            case RHICompareOperation::Less: return VK_COMPARE_OP_LESS;
            case RHICompareOperation::Equal: return VK_COMPARE_OP_EQUAL;
            case RHICompareOperation::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case RHICompareOperation::Greater: return VK_COMPARE_OP_GREATER;
            case RHICompareOperation::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
            case RHICompareOperation::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case RHICompareOperation::Always: return VK_COMPARE_OP_ALWAYS;
            }
            return VK_COMPARE_OP_ALWAYS;
        }

        VkStencilOp to_vk_stencil_operation(RHIStencilOperation operation)
        {
            switch (operation)
            {
            case RHIStencilOperation::Keep: return VK_STENCIL_OP_KEEP;
            case RHIStencilOperation::Zero: return VK_STENCIL_OP_ZERO;
            case RHIStencilOperation::Replace: return VK_STENCIL_OP_REPLACE;
            case RHIStencilOperation::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            case RHIStencilOperation::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            case RHIStencilOperation::Invert: return VK_STENCIL_OP_INVERT;
            case RHIStencilOperation::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
            case RHIStencilOperation::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
            }
            return VK_STENCIL_OP_KEEP;
        }

        VkStencilOpState to_vk_stencil_face(
            const RHIGraphicsPipelineDesc::StencilFaceState& face,
            std::uint8_t read_mask,
            std::uint8_t write_mask)
        {
            VkStencilOpState result{};
            result.failOp = to_vk_stencil_operation(face.fail_operation);
            result.passOp = to_vk_stencil_operation(face.pass_operation);
            result.depthFailOp = to_vk_stencil_operation(face.depth_fail_operation);
            result.compareOp = to_vk_compare_operation(face.compare_operation);
            result.compareMask = read_mask;
            result.writeMask = write_mask;
            result.reference = 0;
            return result;
        }

        VkBorderColor to_vk_border_color(RHIBorderColor color)
        {
            switch (color)
            {
            case RHIBorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            case RHIBorderColor::OpaqueBlack: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            case RHIBorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            }
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        }

        VkBufferUsageFlags to_vk_buffer_usage(RHIResourceUsage usage)
        {
            VkBufferUsageFlags result = 0;
            if (rhi_has_any_flag(usage, RHIResourceUsage::VertexBuffer))
            {
                result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::IndexBuffer))
            {
                result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::UniformBuffer))
            {
                result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::ShaderResource) ||
                rhi_has_any_flag(usage, RHIResourceUsage::UnorderedAccess))
            {
                result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::IndirectArguments))
            {
                result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::CopySource))
            {
                result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::CopyDestination))
            {
                result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            }
            return result;
        }

        VkImageUsageFlags to_vk_image_usage(RHIResourceUsage usage)
        {
            VkImageUsageFlags result = 0;
            if (rhi_has_any_flag(usage, RHIResourceUsage::ShaderResource))
            {
                result |= VK_IMAGE_USAGE_SAMPLED_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::UnorderedAccess))
            {
                result |= VK_IMAGE_USAGE_STORAGE_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::RenderTarget))
            {
                result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::DepthStencil))
            {
                result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::CopySource))
            {
                result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }
            if (rhi_has_any_flag(usage, RHIResourceUsage::CopyDestination))
            {
                result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }
            return result;
        }

        RHIResult<VkImageType> to_vk_image_type(RHIResourceDimension dimension)
        {
            switch (dimension)
            {
            case RHIResourceDimension::Texture1D:
                return RHIResult<VkImageType>::success(VK_IMAGE_TYPE_1D);
            case RHIResourceDimension::Texture2D:
                return RHIResult<VkImageType>::success(VK_IMAGE_TYPE_2D);
            case RHIResourceDimension::Texture3D:
                return RHIResult<VkImageType>::success(VK_IMAGE_TYPE_3D);
            default:
                return RHIResult<VkImageType>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan texture creation requires a texture resource dimension.");
            }
        }

        RHIResult<VkSampleCountFlagBits> to_vk_sample_count(std::uint32_t sample_count)
        {
            switch (sample_count)
            {
            case 1:
                return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_1_BIT);
            case 2:
                return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_2_BIT);
            case 4:
                return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_4_BIT);
            case 8:
                return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_8_BIT);
            case 16:
                return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_16_BIT);
            case 32:
                return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_32_BIT);
            case 64:
                return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_64_BIT);
            default:
                return RHIResult<VkSampleCountFlagBits>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan does not support the requested texture sample count.");
            }
        }

        RHIResult<VkImageAspectFlags> to_vk_image_aspect(
            RHITextureAspect aspect,
            VkFormat format)
        {
            const bool has_depth = is_vk_depth_format(format);
            const bool has_stencil = is_vk_stencil_format(format);
            switch (aspect)
            {
            case RHITextureAspect::Color:
                if (!has_depth)
                {
                    return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_COLOR_BIT);
                }
                break;
            case RHITextureAspect::Depth:
                if (has_depth)
                {
                    return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_DEPTH_BIT);
                }
                break;
            case RHITextureAspect::Stencil:
                if (has_stencil)
                {
                    return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_STENCIL_BIT);
                }
                break;
            case RHITextureAspect::DepthStencil:
                if (has_depth && has_stencil)
                {
                    return RHIResult<VkImageAspectFlags>::success(
                        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
                }
                break;
            }
            return RHIResult<VkImageAspectFlags>::failure(
                RHIErrorCode::InvalidArgument,
                "Texture aspect is incompatible with the Vulkan image format.");
        }

        RHIResult<VkImageViewType> to_vk_image_view_type(const RHITextureViewDesc& desc)
        {
            switch (desc.dimension)
            {
            case RHITextureViewDimension::Texture1D:
                return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_1D);
            case RHITextureViewDimension::Texture1DArray:
                return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_1D_ARRAY);
            case RHITextureViewDimension::Texture2D:
            case RHITextureViewDimension::Texture2DMS:
                return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_2D);
            case RHITextureViewDimension::Texture2DArray:
            case RHITextureViewDimension::Texture2DMSArray:
                return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_2D_ARRAY);
            case RHITextureViewDimension::Texture3D:
                return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_3D);
            case RHITextureViewDimension::TextureCube:
                return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_CUBE);
            case RHITextureViewDimension::TextureCubeArray:
                return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);
            }
            return RHIResult<VkImageViewType>::failure(
                RHIErrorCode::Unsupported,
                "The requested RHI texture-view dimension has no Vulkan mapping.");
        }

    }

    VulkanDevice::VulkanDevice() = default;

    VulkanDevice::~VulkanDevice()
    {
        shutdown();
    }

    RHIStatus VulkanDevice::initialize(const RHIDeviceDesc& desc)
    {
        if (initialized)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Vulkan device is already initialized.");
        }

        const RHIStatus validation = validate_device_desc(desc);
        if (!validation)
        {
            return validation;
        }

        primary_rhi_surface = desc.primary_surface;

        RHIStatus status = create_instance(desc);
        if (!status)
        {
            shutdown();
            return status;
        }
        status = create_primary_surface(desc.primary_surface->desc());
        if (!status)
        {
            shutdown();
            return status;
        }
        status = select_physical_device();
        if (!status)
        {
            shutdown();
            return status;
        }
        status = create_logical_device();
        if (!status)
        {
            shutdown();
            return status;
        }

        query_capabilities_and_limits();
        memory_manager_instance = std::make_unique<VulkanMemoryManager>();
        VulkanMemoryManagerDesc memory_manager_desc;
        memory_manager_desc.instance = vk_instance;
        memory_manager_desc.physical_device = vk_physical_device;
        memory_manager_desc.device = vk_device;
        memory_manager_desc.vulkan_api_version = VK_API_VERSION_1_1;
        status = memory_manager_instance->initialize(memory_manager_desc);
        if (!status)
        {
            shutdown();
            return status;
        }
        upload_manager_instance = std::make_unique<VulkanUploadManager>(*memory_manager_instance);
        deletion_queue = std::make_unique<VulkanDeferredDeletionQueue>();
        queue = std::make_unique<VulkanQueue>(
            vk_device,
            vk_graphics_queue,
            *upload_manager_instance);
        initialized = true;
        return RHIStatus::success();
    }

    RHIStatus VulkanDevice::wait_idle_before_shutdown_impl()
    {
        if (vk_device == VK_NULL_HANDLE)
        {
            return RHIStatus::success();
        }
        return make_vulkan_status(vkDeviceWaitIdle(vk_device), "vkDeviceWaitIdle");
    }

    bool VulkanDevice::is_initialized_impl() const
    {
        return initialized;
    }

    RHIStatus VulkanDevice::shutdown_impl()
    {
        queue.reset();
        if (deletion_queue && vk_device != VK_NULL_HANDLE)
        {
            deletion_queue->release_all(vk_device);
        }
        deletion_queue.reset();
        if (upload_manager_instance)
        {
            upload_manager_instance->shutdown();
        }
        upload_manager_instance.reset();
        memory_manager_instance.reset();
        if (vk_device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(vk_device, nullptr);
            vk_device = VK_NULL_HANDLE;
        }
        if (primary_surface != VK_NULL_HANDLE && vk_instance != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(vk_instance, primary_surface, nullptr);
            primary_surface = VK_NULL_HANDLE;
        }
        destroy_debug_messenger();
        if (vk_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(vk_instance, nullptr);
            vk_instance = VK_NULL_HANDLE;
        }

        vk_physical_device = VK_NULL_HANDLE;
        vk_graphics_queue = VK_NULL_HANDLE;
        graphics_queue_family = VK_QUEUE_FAMILY_IGNORED;
        primary_rhi_surface.reset();
        device_capabilities = {};
        device_limits = {};
        initialized = false;
        return RHIStatus::success();
    }

    const RHICapabilities& VulkanDevice::capabilities() const
    {
        return device_capabilities;
    }

    const RHILimits& VulkanDevice::limits() const
    {
        return device_limits;
    }

    RHIFormatCapabilities VulkanDevice::format_capabilities(RHIFormat format) const
    {
        RHIFormatCapabilities result;
        if (vk_physical_device == VK_NULL_HANDLE)
        {
            return result;
        }

        const VkFormat vk_format = to_vk_format(format);
        if (vk_format == VK_FORMAT_UNDEFINED)
        {
            return result;
        }

        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(vk_physical_device, vk_format, &properties);
        const VkFormatFeatureFlags features = properties.optimalTilingFeatures;
        if ((features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0)
        {
            result.usage = rhi_enum_or(result.usage, RHIFormatUsage::Sampled);
        }
        if ((features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0)
        {
            result.usage = rhi_enum_or(result.usage, RHIFormatUsage::Storage);
        }
        if ((features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0)
        {
            result.usage = rhi_enum_or(result.usage, RHIFormatUsage::RenderTarget);
        }
        if ((features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        {
            result.usage = rhi_enum_or(result.usage, RHIFormatUsage::DepthStencil);
        }
        if ((features & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0)
        {
            result.usage = rhi_enum_or(result.usage, RHIFormatUsage::VertexBuffer);
        }
        if ((features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0)
        {
            result.usage = rhi_enum_or(result.usage, RHIFormatUsage::CopySource);
        }
        if ((features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0)
        {
            result.usage = rhi_enum_or(result.usage, RHIFormatUsage::CopyDestination);
        }
        result.supported_sample_counts = VK_SAMPLE_COUNT_1_BIT;
        return result;
    }

    RHIQueue& VulkanDevice::graphics_queue()
    {
        return *queue;
    }

    RHIResult<std::unique_ptr<RHIViewportContext>> VulkanDevice::create_viewport_context(
        const RHISurfaceRef& surface,
        const RHIViewportContextDesc& desc)
    {
        if (!initialized || !surface)
        {
            return RHIResult<std::unique_ptr<RHIViewportContext>>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan viewport context requires an initialized device and surface.");
        }
        if (surface != primary_rhi_surface || primary_surface == VK_NULL_HANDLE)
        {
            return RHIResult<std::unique_ptr<RHIViewportContext>>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan viewport contexts currently support only the primary surface.");
        }
        if (desc.width == 0 || desc.height == 0 || desc.image_count < 2 || desc.format == RHIFormat::Unknown)
        {
            return RHIResult<std::unique_ptr<RHIViewportContext>>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan viewport context descriptor is invalid.");
        }
        return RHIResult<std::unique_ptr<RHIViewportContext>>::success(
            std::make_unique<VulkanViewportContext>(*this, surface, desc));
    }

    RHIResult<RHIBufferRef> VulkanDevice::create_buffer(
        const RHIBufferDesc& desc,
        const RHIInitialData* initial_data)
    {
        const RHIStatus validation = validate_buffer_desc(desc);
        if (!validation)
        {
            return RHIResult<RHIBufferRef>::failure(validation.code(), validation.message());
        }
        if (!initialized || vk_device == VK_NULL_HANDLE)
        {
            return RHIResult<RHIBufferRef>::failure(RHIErrorCode::NotReady, "Vulkan device is not initialized.");
        }
        if (initial_data != nullptr)
        {
            const RHIStatus initial_data_status = validate_buffer_initial_data(desc, *initial_data);
            if (!initial_data_status)
            {
                return RHIResult<RHIBufferRef>::failure(initial_data_status.code(), initial_data_status.message());
            }
            return RHIResult<RHIBufferRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan buffer initial data requires the upload path, which is not implemented yet.");
        }
        if (desc.cpu_access != RHICPUAccess::None)
        {
            return RHIResult<RHIBufferRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan CPU-accessible buffers are not implemented yet.");
        }
        if (desc.initial_access != RHIAccess::Unknown && desc.initial_access != RHIAccess::Common)
        {
            return RHIResult<RHIBufferRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan buffer creation currently supports only Unknown or Common initial access.");
        }

        const VkBufferUsageFlags usage = to_vk_buffer_usage(desc.usage);
        if (usage == 0)
        {
            return RHIResult<RHIBufferRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer creation requires at least one supported usage flag.");
        }
        VkBufferCreateInfo create_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        create_info.size = desc.size;
        create_info.usage = usage;
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        auto allocated_buffer = memory_manager_instance->create_buffer(
            create_info,
            VulkanAllocationUsage::GpuOnly,
            desc.debug_name.c_str());
        if (!allocated_buffer)
        {
            return RHIResult<RHIBufferRef>::failure(
                allocated_buffer.status().code(), allocated_buffer.status().message());
        }
        return RHIResult<RHIBufferRef>::success(
            std::make_shared<VulkanBuffer>(
                desc,
                *memory_manager_instance,
                *deletion_queue,
                std::move(allocated_buffer.value()),
                desc.initial_access));
    }

    RHIResult<RHITextureRef> VulkanDevice::create_texture(
        const RHITextureDesc& desc,
        const RHIInitialData* initial_data)
    {
        const RHIStatus validation = validate_texture_desc(desc);
        if (!validation)
        {
            return RHIResult<RHITextureRef>::failure(validation.code(), validation.message());
        }
        if (!initialized || vk_device == VK_NULL_HANDLE)
        {
            return RHIResult<RHITextureRef>::failure(RHIErrorCode::NotReady, "Vulkan device is not initialized.");
        }
        if (initial_data != nullptr)
        {
            const RHIStatus initial_data_status = validate_texture_initial_data(desc, *initial_data);
            if (!initial_data_status)
            {
                return RHIResult<RHITextureRef>::failure(initial_data_status.code(), initial_data_status.message());
            }
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan texture initial data requires the upload path, which is not implemented yet.");
        }
        if (desc.cpu_access != RHICPUAccess::None)
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan CPU-accessible textures are not implemented yet.");
        }
        if (desc.initial_access != RHIAccess::Unknown && desc.initial_access != RHIAccess::Common)
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan texture creation currently supports only Unknown or Common initial access.");
        }

        const VkFormat format = to_vk_format(desc.format);
        if (format == VK_FORMAT_UNDEFINED)
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::Unsupported,
                "The requested RHI texture format has no Vulkan mapping.");
        }
        const RHIStatus format_status = validate_texture_format_capabilities(
            desc, format_capabilities(desc.format));
        if (!format_status)
        {
            return RHIResult<RHITextureRef>::failure(
                format_status.code(), format_status.message());
        }
        const VkImageUsageFlags usage = to_vk_image_usage(desc.usage);
        if (usage == 0)
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture creation requires at least one supported usage flag.");
        }
        const auto image_type = to_vk_image_type(desc.dimension);
        if (!image_type)
        {
            return RHIResult<RHITextureRef>::failure(image_type.status().code(), image_type.status().message());
        }
        const auto sample_count = to_vk_sample_count(desc.sample_count);
        if (!sample_count)
        {
            return RHIResult<RHITextureRef>::failure(sample_count.status().code(), sample_count.status().message());
        }

        VkImageFormatProperties image_format_properties{};
        const VkResult format_properties_result = vkGetPhysicalDeviceImageFormatProperties(
            vk_physical_device,
            format,
            image_type.value(),
            VK_IMAGE_TILING_OPTIMAL,
            usage,
            0,
            &image_format_properties);
        if (format_properties_result == VK_ERROR_FORMAT_NOT_SUPPORTED ||
            (format_properties_result == VK_SUCCESS &&
                (image_format_properties.sampleCounts & sample_count.value()) == 0))
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan does not support the requested texture format, combined usage, and sample count.");
        }
        if (format_properties_result != VK_SUCCESS)
        {
            return RHIResult<RHITextureRef>::failure(
                make_vulkan_status(
                    format_properties_result,
                    "vkGetPhysicalDeviceImageFormatProperties").code(),
                "vkGetPhysicalDeviceImageFormatProperties failed while validating texture support.");
        }

        VkImageCreateInfo create_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        create_info.imageType = image_type.value();
        create_info.format = format;
        create_info.extent = {desc.width, desc.height, desc.depth};
        create_info.mipLevels = desc.mip_levels;
        create_info.arrayLayers = desc.array_layers;
        create_info.samples = sample_count.value();
        create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        create_info.usage = usage;
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        auto allocated_image = memory_manager_instance->create_image(
            create_info,
            VulkanAllocationUsage::GpuOnly,
            desc.debug_name.c_str());
        if (!allocated_image)
        {
            return RHIResult<RHITextureRef>::failure(
                allocated_image.status().code(), allocated_image.status().message());
        }
        return RHIResult<RHITextureRef>::success(std::make_shared<VulkanTexture>(
            desc,
            *memory_manager_instance,
            *deletion_queue,
            std::move(allocated_image.value()),
            VK_IMAGE_LAYOUT_UNDEFINED,
            desc.initial_access));
    }

    RHIResult<RHIBufferViewRef> VulkanDevice::create_buffer_view(const RHIBufferRef&, const RHIBufferViewDesc&)
    {
        return RHIResult<RHIBufferViewRef>::failure(
            RHIErrorCode::Unsupported,
            "Vulkan buffer views require descriptor binding support, which is not implemented yet.");
    }

    RHIResult<RHITextureViewRef> VulkanDevice::create_texture_view(
        const RHITextureRef& texture,
        const RHITextureViewDesc& desc)
    {
        if (!texture)
        {
            return RHIResult<RHITextureViewRef>::failure(RHIErrorCode::InvalidArgument, "Vulkan texture view requires a texture.");
        }
        if (!initialized || vk_device == VK_NULL_HANDLE)
        {
            return RHIResult<RHITextureViewRef>::failure(RHIErrorCode::NotReady, "Vulkan device is not initialized.");
        }
        const RHIStatus validation = validate_texture_view_desc(texture->desc(), desc);
        if (!validation)
        {
            return RHIResult<RHITextureViewRef>::failure(validation.code(), validation.message());
        }
        const auto vulkan_texture = std::dynamic_pointer_cast<VulkanTexture>(texture);
        if (!vulkan_texture)
        {
            return RHIResult<RHITextureViewRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture views require a texture created by the Vulkan device.");
        }
        const VkFormat view_format = vulkan_format_from_rhi(desc.format);
        if (view_format == VK_FORMAT_UNDEFINED || view_format != vulkan_format_from_rhi(texture->desc().format))
        {
            return RHIResult<RHITextureViewRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan texture views currently require the texture's original format.");
        }
        const auto view_type = to_vk_image_view_type(desc);
        if (!view_type)
        {
            return RHIResult<RHITextureViewRef>::failure(view_type.status().code(), view_type.status().message());
        }
        const auto aspect = to_vk_image_aspect(desc.subresources.aspect, view_format);
        if (!aspect)
        {
            return RHIResult<RHITextureViewRef>::failure(aspect.status().code(), aspect.status().message());
        }
        if (desc.type == RHIResourceViewType::DepthStencil)
        {
            const RHITextureAspect required_aspect = is_vk_stencil_format(view_format)
                ? RHITextureAspect::DepthStencil
                : RHITextureAspect::Depth;
            if (desc.subresources.aspect != required_aspect)
            {
                return RHIResult<RHITextureViewRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan depth-stencil views must select every aspect present in the attachment format.");
            }
            if (is_vk_stencil_format(view_format) &&
                desc.depth_read_only != desc.stencil_read_only)
            {
                return RHIResult<RHITextureViewRef>::failure(
                    RHIErrorCode::Unsupported,
                    "VulkanPortable v1 does not require separate depth and stencil layouts; "
                    "mixed read-only and writable aspects are unsupported by this backend path.");
            }
        }
        const std::uint32_t mip_count = desc.subresources.mip_count == RHI_ALL_MIPS
            ? texture->desc().mip_levels - desc.subresources.first_mip
            : desc.subresources.mip_count;
        const std::uint32_t layer_count = desc.subresources.layer_count == RHI_ALL_LAYERS
            ? texture->desc().array_layers - desc.subresources.first_layer
            : desc.subresources.layer_count;
        if (mip_count == 0 || layer_count == 0 ||
            mip_count > texture->desc().mip_levels - desc.subresources.first_mip ||
            layer_count > texture->desc().array_layers - desc.subresources.first_layer)
        {
            return RHIResult<RHITextureViewRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture view subresource range is outside the texture.");
        }

        VkImageViewCreateInfo create_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        create_info.image = vulkan_texture->image();
        create_info.viewType = view_type.value();
        create_info.format = view_format;
        create_info.subresourceRange.aspectMask = aspect.value();
        create_info.subresourceRange.baseMipLevel = desc.subresources.first_mip;
        create_info.subresourceRange.levelCount = mip_count;
        create_info.subresourceRange.baseArrayLayer = desc.subresources.first_layer;
        create_info.subresourceRange.layerCount = layer_count;
        VkImageView image_view = VK_NULL_HANDLE;
        const RHIStatus status = make_vulkan_status(
            vkCreateImageView(vk_device, &create_info, nullptr, &image_view),
            "vkCreateImageView");
        if (!status)
        {
            return RHIResult<RHITextureViewRef>::failure(status.code(), status.message());
        }
        return RHIResult<RHITextureViewRef>::success(std::make_shared<VulkanTextureView>(
            texture,
            desc,
            vk_device,
            image_view,
            true));
    }

    RHIResult<RHIShaderRef> VulkanDevice::create_shader_impl(const RHIShaderDesc& desc)
    {
        if (!initialized || vk_device == VK_NULL_HANDLE)
        {
            return RHIResult<RHIShaderRef>::failure(RHIErrorCode::NotReady, "Vulkan device is not initialized.");
        }
        std::string target = desc.bytecode.target;
        std::transform(target.begin(), target.end(), target.begin(), [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });
        if (target != "spirv" && target != "spir-v")
        {
            return RHIResult<RHIShaderRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan shaders require SPIR-V bytecode.");
        }
        if (desc.bytecode.bytes.size() % sizeof(std::uint32_t) != 0)
        {
            return RHIResult<RHIShaderRef>::failure(
                RHIErrorCode::InvalidArgument,
                "SPIR-V bytecode size must be a multiple of four bytes.");
        }
        std::vector<std::uint32_t> words(desc.bytecode.bytes.size() / sizeof(std::uint32_t));
        std::memcpy(words.data(), desc.bytecode.bytes.data(), desc.bytecode.bytes.size());
        VkShaderModuleCreateInfo create_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        create_info.codeSize = desc.bytecode.bytes.size();
        create_info.pCode = words.data();
        VkShaderModule shader_module = VK_NULL_HANDLE;
        const RHIStatus status = make_vulkan_status(
            vkCreateShaderModule(vk_device, &create_info, nullptr, &shader_module),
            "vkCreateShaderModule");
        if (!status)
        {
            return RHIResult<RHIShaderRef>::failure(status.code(), status.message());
        }
        return RHIResult<RHIShaderRef>::success(std::make_shared<VulkanShader>(desc, vk_device, shader_module));
    }

    RHIResult<RHIBindingLayoutRef> VulkanDevice::create_binding_layout_impl(
        const RHIBindingLayoutDesc& desc)
    {
        if (!initialized)
        {
            return RHIResult<RHIBindingLayoutRef>::failure(RHIErrorCode::NotReady, "Vulkan device is not initialized.");
        }
        std::array<
            std::vector<VkDescriptorSetLayoutBinding>,
            VulkanBindingLayout::physical_set_count> group_bindings;
        std::vector<VulkanBindingLayout::NativeBinding> native_bindings;
        std::set<std::pair<std::uint32_t, std::uint32_t>> occupied_bindings;
        native_bindings.reserve(desc.entries.size());
        for (const RHIBindingLayoutEntry& entry : desc.entries)
        {
            const std::size_t group_index = VulkanBindingLayout::physical_set(entry.group);
            if (group_index >= group_bindings.size())
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan binding layout contains an invalid binding group.");
            }
            const VkDescriptorType descriptor_type = to_vk_descriptor_type(entry.type);
            if (descriptor_type == VK_DESCRIPTOR_TYPE_MAX_ENUM)
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan binding layout contains an unsupported resource type.");
            }
            const std::uint32_t native_binding = entry.slot;
            if (!occupied_bindings.emplace(
                    static_cast<std::uint32_t>(group_index), native_binding).second)
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan binding layout contains duplicate bindings in one physical set.");
            }
            VkDescriptorSetLayoutBinding layout_binding{};
            layout_binding.binding = native_binding;
            layout_binding.descriptorType = descriptor_type;
            layout_binding.descriptorCount = entry.array_count;
            layout_binding.stageFlags = to_vk_shader_stage_flags(entry.stages);
            group_bindings[group_index].push_back(layout_binding);
            native_bindings.push_back({entry.group, entry.slot, entry.type, native_binding});
        }

        std::array<
            VkDescriptorSetLayout,
            VulkanBindingLayout::physical_set_count> descriptor_set_layouts{};
        for (std::size_t group_index = 0; group_index < group_bindings.size(); ++group_index)
        {
            VkDescriptorSetLayoutCreateInfo create_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            create_info.bindingCount = static_cast<std::uint32_t>(group_bindings[group_index].size());
            create_info.pBindings = group_bindings[group_index].data();
            const RHIStatus create_status = make_vulkan_status(
                vkCreateDescriptorSetLayout(vk_device, &create_info, nullptr, &descriptor_set_layouts[group_index]),
                "vkCreateDescriptorSetLayout");
            if (!create_status)
            {
                for (VkDescriptorSetLayout layout : descriptor_set_layouts)
                {
                    if (layout != VK_NULL_HANDLE)
                    {
                        vkDestroyDescriptorSetLayout(vk_device, layout, nullptr);
                    }
                }
                return RHIResult<RHIBindingLayoutRef>::failure(create_status.code(), create_status.message());
            }
        }
        return RHIResult<RHIBindingLayoutRef>::success(std::make_shared<VulkanBindingLayout>(
            desc, vk_device, descriptor_set_layouts, std::move(native_bindings)));
    }

    RHIResult<RHISamplerRef> VulkanDevice::create_sampler(const RHISamplerDesc& desc)
    {
        const RHIStatus validation = validate_sampler_desc(desc);
        if (!validation)
        {
            return RHIResult<RHISamplerRef>::failure(validation.code(), validation.message());
        }
        if (!initialized)
        {
            return RHIResult<RHISamplerRef>::failure(RHIErrorCode::NotReady, "Vulkan device is not initialized.");
        }
        if (desc.max_anisotropy > device_limits.max_sampler_anisotropy)
        {
            return RHIResult<RHISamplerRef>::failure(
                RHIErrorCode::Unsupported,
                "Requested sampler anisotropy exceeds the Vulkan device limit.");
        }

        VkSamplerCreateInfo create_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        create_info.magFilter = to_vk_filter(desc.mag_filter);
        create_info.minFilter = to_vk_filter(desc.min_filter);
        create_info.mipmapMode = to_vk_mipmap_mode(desc.mip_filter);
        create_info.addressModeU = to_vk_address_mode(desc.address_u);
        create_info.addressModeV = to_vk_address_mode(desc.address_v);
        create_info.addressModeW = to_vk_address_mode(desc.address_w);
        create_info.mipLodBias = desc.mip_lod_bias;
        create_info.anisotropyEnable = desc.max_anisotropy > 1 ? VK_TRUE : VK_FALSE;
        create_info.maxAnisotropy = static_cast<float>(desc.max_anisotropy);
        create_info.compareEnable = desc.compare_enable ? VK_TRUE : VK_FALSE;
        create_info.compareOp = to_vk_compare_operation(desc.compare_operation);
        create_info.minLod = desc.min_lod;
        create_info.maxLod = desc.max_lod;
        create_info.borderColor = to_vk_border_color(desc.border_color);
        VkSampler sampler = VK_NULL_HANDLE;
        const RHIStatus create_status = make_vulkan_status(
            vkCreateSampler(vk_device, &create_info, nullptr, &sampler),
            "vkCreateSampler");
        if (!create_status)
        {
            return RHIResult<RHISamplerRef>::failure(create_status.code(), create_status.message());
        }
        return RHIResult<RHISamplerRef>::success(std::make_shared<VulkanSampler>(desc, vk_device, sampler));
    }

    RHIResult<RHIBindingSetRef> VulkanDevice::create_binding_set(const RHIBindingSetDesc& desc)
    {
        const RHIStatus validation = validate_binding_set_desc(desc);
        if (!validation)
        {
            return RHIResult<RHIBindingSetRef>::failure(validation.code(), validation.message());
        }
        if (!initialized)
        {
            return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::NotReady, "Vulkan device is not initialized.");
        }
        const auto layout = std::dynamic_pointer_cast<VulkanBindingLayout>(desc.layout);
        if (!layout)
        {
            return RHIResult<RHIBindingSetRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan binding set requires a layout created by the Vulkan device.");
        }
        for (const RHIBindingLayoutEntry& entry : desc.layout->desc().entries)
        {
            if (entry.group != desc.group)
            {
                continue;
            }
            if (entry.type == RHIResourceBindingType::ReadOnlyBuffer ||
                entry.type == RHIResourceBindingType::StorageBuffer ||
                entry.type == RHIResourceBindingType::StorageTexture)
            {
                return RHIResult<RHIBindingSetRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan binding sets currently support uniform buffers, sampled textures, and samplers only.");
            }
        }
        for (const RHIBindingValue& value : desc.bindings)
        {
            if (value.buffer)
            {
                const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(value.buffer);
                if (!buffer || value.buffer_offset % device_limits.uniform_buffer_offset_alignment != 0)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan uniform-buffer binding requires a Vulkan buffer and an aligned offset.");
                }
            }
            else if (value.texture_view)
            {
                const auto view = std::dynamic_pointer_cast<VulkanTextureView>(value.texture_view);
                if (!view)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan sampled-texture binding requires a Vulkan texture view.");
                }
            }
            else if (value.sampler)
            {
                const auto sampler = std::dynamic_pointer_cast<VulkanSampler>(value.sampler);
                if (!sampler)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan sampler binding requires a sampler created by the Vulkan device.");
                }
            }
            else
            {
                return RHIResult<RHIBindingSetRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan buffer-view and storage bindings are not implemented yet.");
            }
        }
        return RHIResult<RHIBindingSetRef>::success(
            std::make_shared<VulkanBindingSet>(desc));
    }

    RHIResult<std::shared_ptr<VulkanBindingPacket>> VulkanDevice::materialize_binding_packet(
        const std::shared_ptr<VulkanBindingLayout>& layout,
        std::uint32_t physical_set,
        const std::vector<std::shared_ptr<VulkanBindingSet>>& logical_sets)
    {
        if (!initialized || !layout || logical_sets.empty() ||
            physical_set >= VulkanBindingLayout::physical_set_count)
        {
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan binding packet requires an initialized device, layout, physical set, and logical sets.");
        }

        std::size_t binding_value_count = 0;
        std::map<VkDescriptorType, std::uint32_t> descriptor_counts;
        for (const std::shared_ptr<VulkanBindingSet>& logical_set : logical_sets)
        {
            if (!logical_set ||
                !(logical_set->layout()->desc() == layout->desc()) ||
                VulkanBindingLayout::physical_set(logical_set->group()) != physical_set)
            {
                return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan binding packet contains an incompatible logical binding set.");
            }
            binding_value_count += logical_set->desc().bindings.size();
            for (const RHIBindingLayoutEntry& entry : layout->desc().entries)
            {
                if (entry.group == logical_set->group())
                {
                    descriptor_counts[to_vk_descriptor_type(entry.type)] += entry.array_count;
                }
            }
        }

        std::vector<VkDescriptorPoolSize> pool_sizes;
        pool_sizes.reserve(descriptor_counts.size());
        for (const auto& descriptor_count : descriptor_counts)
        {
            pool_sizes.push_back({descriptor_count.first, descriptor_count.second});
        }
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        RHIStatus status = make_vulkan_status(
            vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &descriptor_pool),
            "vkCreateDescriptorPool");
        if (!status)
        {
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                status.code(), status.message());
        }

        const RHIBindingGroup representative_group = logical_sets.front()->group();
        const VkDescriptorSetLayout set_layout = layout->descriptor_set_layout(representative_group);
        VkDescriptorSetAllocateInfo allocate_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate_info.descriptorPool = descriptor_pool;
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts = &set_layout;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        status = make_vulkan_status(
            vkAllocateDescriptorSets(vk_device, &allocate_info, &descriptor_set),
            "vkAllocateDescriptorSets");
        if (!status)
        {
            vkDestroyDescriptorPool(vk_device, descriptor_pool, nullptr);
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                status.code(), status.message());
        }

        std::vector<VkDescriptorBufferInfo> buffer_infos;
        std::vector<VkDescriptorImageInfo> image_infos;
        std::vector<VkWriteDescriptorSet> writes;
        buffer_infos.reserve(binding_value_count);
        image_infos.reserve(binding_value_count);
        writes.reserve(binding_value_count);
        for (const std::shared_ptr<VulkanBindingSet>& logical_set : logical_sets)
        {
            for (const RHIBindingValue& value : logical_set->desc().bindings)
            {
                RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
                VkDescriptorBufferInfo* buffer_info = nullptr;
                VkDescriptorImageInfo* image_info = nullptr;
                if (value.buffer)
                {
                    const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(value.buffer);
                    const std::uint64_t range = value.buffer_size == 0
                        ? value.buffer->desc().size - value.buffer_offset
                        : value.buffer_size;
                    buffer_infos.push_back({buffer->buffer(), value.buffer_offset, range});
                    buffer_info = &buffer_infos.back();
                }
                else if (value.texture_view)
                {
                    type = RHIResourceBindingType::SampledTexture;
                    const auto view = std::dynamic_pointer_cast<VulkanTextureView>(value.texture_view);
                    image_infos.push_back({
                        VK_NULL_HANDLE, view->image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                    image_info = &image_infos.back();
                }
                else if (value.sampler)
                {
                    type = RHIResourceBindingType::Sampler;
                    const auto sampler = std::dynamic_pointer_cast<VulkanSampler>(value.sampler);
                    image_infos.push_back({
                        sampler->sampler(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
                    image_info = &image_infos.back();
                }
                else
                {
                    vkDestroyDescriptorPool(vk_device, descriptor_pool, nullptr);
                    return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                        RHIErrorCode::Unsupported,
                        "Vulkan buffer-view and storage bindings are not implemented yet.");
                }

                const auto native_binding = layout->native_binding(
                    logical_set->group(), type, value.slot);
                if (!native_binding)
                {
                    vkDestroyDescriptorPool(vk_device, descriptor_pool, nullptr);
                    return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                        native_binding.status().code(), native_binding.status().message());
                }
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = descriptor_set;
                write.dstBinding = native_binding.value();
                write.dstArrayElement = value.array_index;
                write.descriptorCount = 1;
                write.descriptorType = to_vk_descriptor_type(type);
                write.pBufferInfo = buffer_info;
                write.pImageInfo = image_info;
                writes.push_back(write);
            }
        }
        vkUpdateDescriptorSets(vk_device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        return RHIResult<std::shared_ptr<VulkanBindingPacket>>::success(
            std::make_shared<VulkanBindingPacket>(
                vk_device, descriptor_pool, descriptor_set, logical_sets));
    }

    RHIResult<RHIGraphicsPipelineRef> VulkanDevice::create_graphics_pipeline_impl(const RHIGraphicsPipelineDesc& desc)
    {
        if (!initialized || vk_device == VK_NULL_HANDLE)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(RHIErrorCode::NotReady, "Vulkan device is not initialized.");
        }
        const auto vertex_shader = std::dynamic_pointer_cast<VulkanShader>(desc.vertex_shader);
        const auto pixel_shader = std::dynamic_pointer_cast<VulkanShader>(desc.pixel_shader);
        const auto binding_layout = std::dynamic_pointer_cast<VulkanBindingLayout>(desc.binding_layout);
        if (!vertex_shader || !pixel_shader || !binding_layout)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics pipelines require shaders and a binding layout created by the Vulkan device.");
        }
        if (desc.color_attachment_count == 0 && desc.depth_stencil_format == RHIFormat::Unknown)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics pipelines require at least one color or depth-stencil attachment.");
        }
        if (desc.rasterization.depth_clamp_enable || desc.rasterization.polygon_mode != RHIPolygonMode::Fill)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan graphics pipelines currently support filled primitives without depth clamp only.");
        }

        const auto primitive_topology = to_vk_primitive_topology(desc.primitive_topology);
        const auto cull_mode = to_vk_cull_mode(desc.rasterization.cull_mode);
        const auto front_face = to_vk_front_face(desc.rasterization.front_face);
        const auto sample_count = to_vk_sample_count(desc.sample_count);
        if (!primitive_topology || !cull_mode || !front_face || !sample_count)
        {
            const RHIStatus& status = !primitive_topology ? primitive_topology.status() :
                (!cull_mode ? cull_mode.status() : (!front_face ? front_face.status() : sample_count.status()));
            return RHIResult<RHIGraphicsPipelineRef>::failure(status.code(), status.message());
        }

        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> attachment_references;
        std::vector<VkPipelineColorBlendAttachmentState> blend_attachments;
        attachments.reserve(desc.color_attachment_count +
            (desc.depth_stencil_format != RHIFormat::Unknown ? 1U : 0U));
        attachment_references.reserve(desc.color_attachment_count);
        blend_attachments.reserve(desc.color_attachment_count);
        for (std::uint32_t index = 0; index < desc.color_attachment_count; ++index)
        {
            const VkFormat format = to_vk_format(desc.color_formats[index]);
            if (format == VK_FORMAT_UNDEFINED)
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "A Vulkan graphics pipeline color attachment format has no Vulkan mapping.");
            }
            VkAttachmentDescription attachment{};
            attachment.format = format;
            attachment.samples = sample_count.value();
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments.push_back(attachment);
            attachment_references.push_back({index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

            const auto& blend = desc.color_blend_attachments[index];
            const auto source_color = to_vk_blend_factor(blend.source_color_factor);
            const auto destination_color = to_vk_blend_factor(blend.destination_color_factor);
            const auto source_alpha = to_vk_blend_factor(blend.source_alpha_factor);
            const auto destination_alpha = to_vk_blend_factor(blend.destination_alpha_factor);
            const auto color_operation = to_vk_blend_operation(blend.color_operation);
            const auto alpha_operation = to_vk_blend_operation(blend.alpha_operation);
            if (!source_color || !destination_color || !source_alpha || !destination_alpha ||
                !color_operation || !alpha_operation)
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan graphics pipeline has an invalid color blend state.");
            }
            VkPipelineColorBlendAttachmentState vk_blend{};
            vk_blend.blendEnable = blend.blend_enable ? VK_TRUE : VK_FALSE;
            vk_blend.srcColorBlendFactor = source_color.value();
            vk_blend.dstColorBlendFactor = destination_color.value();
            vk_blend.colorBlendOp = color_operation.value();
            vk_blend.srcAlphaBlendFactor = source_alpha.value();
            vk_blend.dstAlphaBlendFactor = destination_alpha.value();
            vk_blend.alphaBlendOp = alpha_operation.value();
            vk_blend.colorWriteMask = to_vk_color_write_mask(blend.color_write_mask);
            blend_attachments.push_back(vk_blend);
        }

        VkAttachmentReference depth_stencil_reference{};
        const bool has_depth_stencil_attachment = desc.depth_stencil_format != RHIFormat::Unknown;
        if (has_depth_stencil_attachment)
        {
            const VkFormat format = to_vk_format(desc.depth_stencil_format);
            if (!is_vk_depth_format(format))
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "The Vulkan graphics pipeline depth-stencil format is not a supported depth format.");
            }
            if (desc.depth_stencil.stencil_test_enable && !is_vk_stencil_format(format))
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan stencil testing requires a depth-stencil format with a stencil aspect.");
            }
            VkAttachmentDescription attachment{};
            attachment.format = format;
            attachment.samples = sample_count.value();
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth_stencil_reference.attachment = static_cast<std::uint32_t>(attachments.size());
            depth_stencil_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments.push_back(attachment);
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(attachment_references.size());
        subpass.pColorAttachments = attachment_references.empty()
            ? nullptr
            : attachment_references.data();
        subpass.pDepthStencilAttachment = has_depth_stencil_attachment
            ? &depth_stencil_reference
            : nullptr;
        VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        render_pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        render_pass_info.pAttachments = attachments.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        VkRenderPass compatibility_render_pass = VK_NULL_HANDLE;
        RHIStatus status = make_vulkan_status(
            vkCreateRenderPass(vk_device, &render_pass_info, nullptr, &compatibility_render_pass),
            "vkCreateRenderPass for graphics pipeline");
        if (!status)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(status.code(), status.message());
        }

        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        const auto& descriptor_set_layouts = binding_layout->descriptor_set_layouts();
        layout_info.setLayoutCount = static_cast<std::uint32_t>(descriptor_set_layouts.size());
        layout_info.pSetLayouts = descriptor_set_layouts.data();
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        status = make_vulkan_status(
            vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &pipeline_layout),
            "vkCreatePipelineLayout");
        if (!status)
        {
            vkDestroyRenderPass(vk_device, compatibility_render_pass, nullptr);
            return RHIResult<RHIGraphicsPipelineRef>::failure(status.code(), status.message());
        }

        std::vector<VkVertexInputBindingDescription> vertex_bindings;
        std::vector<VkVertexInputAttributeDescription> vertex_attributes;
        vertex_bindings.reserve(desc.vertex_buffers.size());
        vertex_attributes.reserve(desc.vertex_attributes.size());
        for (const auto& layout : desc.vertex_buffers)
        {
            const auto input_rate = to_vk_vertex_input_rate(layout.input_rate);
            if (!input_rate)
            {
                vkDestroyPipelineLayout(vk_device, pipeline_layout, nullptr);
                vkDestroyRenderPass(vk_device, compatibility_render_pass, nullptr);
                return RHIResult<RHIGraphicsPipelineRef>::failure(input_rate.status().code(), input_rate.status().message());
            }
            vertex_bindings.push_back({layout.binding, layout.stride, input_rate.value()});
        }
        for (const auto& attribute : desc.vertex_attributes)
        {
            const VkFormat format = to_vk_format(attribute.format);
            if (format == VK_FORMAT_UNDEFINED)
            {
                vkDestroyPipelineLayout(vk_device, pipeline_layout, nullptr);
                vkDestroyRenderPass(vk_device, compatibility_render_pass, nullptr);
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "A Vulkan graphics pipeline vertex attribute format has no Vulkan mapping.");
            }
            // Public validation has already matched this location/format to
            // vertex-shader reflection. Vulkan consumes the location directly;
            // semantic name/index remain available for D3D backends only.
            vertex_attributes.push_back({attribute.location, attribute.binding, format, attribute.offset});
        }

        VkPipelineShaderStageCreateInfo shader_stages[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shader_stages[0].module = vertex_shader->shader_module();
        shader_stages[0].pName = desc.vertex_shader->desc().entry_point.c_str();
        shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shader_stages[1].module = pixel_shader->shader_module();
        shader_stages[1].pName = desc.pixel_shader->desc().entry_point.c_str();
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex_input.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertex_bindings.size());
        vertex_input.pVertexBindingDescriptions = vertex_bindings.data();
        vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertex_attributes.size());
        vertex_input.pVertexAttributeDescriptions = vertex_attributes.data();
        VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        input_assembly.topology = primitive_topology.value();
        VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = cull_mode.value();
        rasterizer.frontFace = front_face.value();
        rasterizer.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = sample_count.value();
        VkPipelineDepthStencilStateCreateInfo depth_stencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth_stencil.depthTestEnable = desc.depth_stencil.depth_test_enable ? VK_TRUE : VK_FALSE;
        depth_stencil.depthWriteEnable = desc.depth_stencil.depth_write_enable ? VK_TRUE : VK_FALSE;
        depth_stencil.depthCompareOp = to_vk_compare_operation(desc.depth_stencil.depth_compare_operation);
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
        depth_stencil.stencilTestEnable = desc.depth_stencil.stencil_test_enable ? VK_TRUE : VK_FALSE;
        depth_stencil.front = to_vk_stencil_face(
            desc.depth_stencil.front_face,
            desc.depth_stencil.stencil_read_mask,
            desc.depth_stencil.stencil_write_mask);
        depth_stencil.back = to_vk_stencil_face(
            desc.depth_stencil.back_face,
            desc.depth_stencil.stencil_read_mask,
            desc.depth_stencil.stencil_write_mask);
        VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        color_blend.attachmentCount = static_cast<std::uint32_t>(blend_attachments.size());
        color_blend.pAttachments = blend_attachments.empty() ? nullptr : blend_attachments.data();
        const VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        VkPipelineDynamicStateCreateInfo dynamic_state{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        // std::size derives the native C-array length so the Vulkan count cannot
        // drift when dynamic states are added or removed.
        dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamic_states));
        dynamic_state.pDynamicStates = dynamic_states;
        VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pDepthStencilState = has_depth_stencil_attachment ? &depth_stencil : nullptr;
        pipeline_info.pColorBlendState = &color_blend;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = pipeline_layout;
        pipeline_info.renderPass = compatibility_render_pass;
        VkPipeline pipeline = VK_NULL_HANDLE;
        status = make_vulkan_status(
            vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline),
            "vkCreateGraphicsPipelines");
        if (!status)
        {
            vkDestroyPipelineLayout(vk_device, pipeline_layout, nullptr);
            vkDestroyRenderPass(vk_device, compatibility_render_pass, nullptr);
            return RHIResult<RHIGraphicsPipelineRef>::failure(status.code(), status.message());
        }
        return RHIResult<RHIGraphicsPipelineRef>::success(std::make_shared<VulkanGraphicsPipeline>(
            desc,
            vk_device,
            compatibility_render_pass,
            pipeline_layout,
            pipeline));
    }

    RHIResult<RHIGPUFenceRef> VulkanDevice::create_gpu_fence(const std::string&)
    {
        return RHIResult<RHIGPUFenceRef>::failure(RHIErrorCode::Unsupported, "Vulkan GPU fences are not implemented yet.");
    }

    RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> VulkanDevice::create_graphics_command_context()
    {
        if (!initialized || vk_device == VK_NULL_HANDLE ||
            graphics_queue_family == VK_QUEUE_FAMILY_IGNORED)
        {
            return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::failure(
                RHIErrorCode::NotReady,
                "Vulkan device-level command context requires an initialized device.");
        }

        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = graphics_queue_family;
        VkCommandPool command_pool = VK_NULL_HANDLE;
        const RHIStatus status = make_vulkan_status(
            vkCreateCommandPool(vk_device, &pool_info, nullptr, &command_pool),
            "vkCreateCommandPool");
        if (!status)
        {
            return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::failure(
                status.code(), status.message());
        }

        auto owned_pool = std::make_shared<VulkanCommandPool>(
            vk_device,
            command_pool);
        return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::success(
            std::make_unique<VulkanGraphicsCommandContext>(
                *this,
                std::move(owned_pool)));
    }

    VkInstance VulkanDevice::instance() const
    {
        return vk_instance;
    }

    VkSurfaceKHR VulkanDevice::primary_surface_handle() const
    {
        return primary_surface;
    }

    VkPhysicalDevice VulkanDevice::physical_device() const
    {
        return vk_physical_device;
    }

    VkDevice VulkanDevice::device() const
    {
        return vk_device;
    }

    VkQueue VulkanDevice::graphics_queue_handle() const
    {
        return vk_graphics_queue;
    }

    std::uint32_t VulkanDevice::graphics_queue_family_index() const
    {
        return graphics_queue_family;
    }

    VulkanMemoryManager& VulkanDevice::memory_manager()
    {
        return *memory_manager_instance;
    }

    VulkanUploadManager& VulkanDevice::upload_manager()
    {
        return *upload_manager_instance;
    }

    VulkanDeferredDeletionQueue& VulkanDevice::deferred_deletion_queue()
    {
        return *deletion_queue;
    }

    VulkanDeviceObservation VulkanDevice::observation_snapshot() const
    {
        VulkanDeviceObservation observation;
        if (memory_manager_instance)
        {
            observation.memory = memory_manager_instance->statistics();
        }
        if (upload_manager_instance)
        {
            observation.upload = upload_manager_instance->statistics();
        }
        if (deletion_queue)
        {
            observation.pending_deletions = deletion_queue->pending_count();
        }
        if (queue)
        {
            observation.completed_value = queue->completed_value();
        }
        return observation;
    }

    void VulkanDevice::release_completed_work(RHIQueueCompletionValue completed_value)
    {
        if (upload_manager_instance)
        {
            upload_manager_instance->release_completed(completed_value);
        }
        if (deletion_queue && vk_device != VK_NULL_HANDLE)
        {
            deletion_queue->release_completed(vk_device, completed_value);
        }
    }

    RHIStatus VulkanDevice::create_instance(const RHIDeviceDesc& desc)
    {
        const RHIStatus environment_status = configure_vulkan_environment(desc.enable_validation);
        if (!environment_status)
        {
            return environment_status;
        }

        std::uint32_t extension_count = 0;
        VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
        if (result != VK_SUCCESS)
        {
            return make_vulkan_status(result, "vkEnumerateInstanceExtensionProperties");
        }
        std::vector<VkExtensionProperties> available_extensions(extension_count);
        result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available_extensions.data());
        if (result != VK_SUCCESS)
        {
            return make_vulkan_status(result, "vkEnumerateInstanceExtensionProperties");
        }

        std::vector<const char*> extensions;
#if WITH_WIN64
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif WITH_MAC
        std::uint32_t glfw_extension_count = 0;
        const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        if (glfw_extensions == nullptr || glfw_extension_count == 0)
        {
            const char* glfw_error = nullptr;
            glfwGetError(&glfw_error);
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                std::string("GLFW did not provide the Vulkan surface extensions required by macOS") +
                    (glfw_error ? std::string(": ") + glfw_error : std::string(".")));
        }
        extensions.assign(glfw_extensions, glfw_extensions + glfw_extension_count);
#else
        return RHIStatus::failure(RHIErrorCode::Unsupported, "Vulkan device does not support this platform.");
#endif

        VkInstanceCreateFlags instance_flags = 0;
#if WITH_MAC
        if (has_instance_extension(
                available_extensions,
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        {
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }
        if (has_instance_extension(available_extensions, portability_enumeration_extension_name))
        {
            extensions.push_back(portability_enumeration_extension_name);
            instance_flags |= enumerate_portability_flag;
        }
#endif
        std::sort(extensions.begin(), extensions.end(), [](const char* lhs, const char* rhs)
        {
            return std::strcmp(lhs, rhs) < 0;
        });
        extensions.erase(
            std::unique(extensions.begin(), extensions.end(), [](const char* lhs, const char* rhs)
            {
                return std::strcmp(lhs, rhs) == 0;
            }),
            extensions.end());
        for (const char* extension : extensions)
        {
            if (!has_instance_extension(available_extensions, extension))
            {
                return RHIStatus::failure(RHIErrorCode::Unsupported, std::string("Required Vulkan instance extension is unavailable: ") + extension);
            }
        }

        std::vector<const char*> layers;
        VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
        if (desc.enable_validation)
        {
            constexpr const char* validation_layer = "VK_LAYER_KHRONOS_validation";
            if (!has_instance_extension(available_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            {
                return RHIStatus::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan validation logging requires VK_EXT_debug_utils.");
            }
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            std::uint32_t layer_count = 0;
            result = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
            if (result != VK_SUCCESS)
            {
                return make_vulkan_status(result, "vkEnumerateInstanceLayerProperties");
            }
            std::vector<VkLayerProperties> available_layers(layer_count);
            result = vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
            if (result != VK_SUCCESS)
            {
                return make_vulkan_status(result, "vkEnumerateInstanceLayerProperties");
            }
            const bool layer_found = std::any_of(
                available_layers.begin(),
                available_layers.end(),
                [](const VkLayerProperties& layer)
                {
                    return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
                });
            if (!layer_found)
            {
                return RHIStatus::failure(RHIErrorCode::Unsupported, "VK_LAYER_KHRONOS_validation is unavailable.");
            }
            layers.push_back(validation_layer);
            debug_create_info = make_debug_messenger_create_info();
        }

        VkApplicationInfo application_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application_info.pApplicationName = desc.debug_name.empty() ? "Toy3d" : desc.debug_name.c_str();
        application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        application_info.pEngineName = "Toy3d";
        application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        application_info.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create_info.flags = instance_flags;
        create_info.pApplicationInfo = &application_info;
        create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();
        create_info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        create_info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        create_info.pNext = desc.enable_validation ? &debug_create_info : nullptr;

        RHIStatus status = make_vulkan_status(
            vkCreateInstance(&create_info, nullptr, &vk_instance),
            "vkCreateInstance");
        if (!status || !desc.enable_validation)
        {
            return status;
        }
        return create_debug_messenger();
    }

    RHIStatus VulkanDevice::create_debug_messenger()
    {
        const auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(vk_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!create_messenger)
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "vkCreateDebugUtilsMessengerEXT is unavailable.");
        }

        const VkDebugUtilsMessengerCreateInfoEXT create_info = make_debug_messenger_create_info();
        return make_vulkan_status(
            create_messenger(vk_instance, &create_info, nullptr, &vk_debug_messenger),
            "vkCreateDebugUtilsMessengerEXT");
    }

    void VulkanDevice::destroy_debug_messenger()
    {
        if (vk_debug_messenger == VK_NULL_HANDLE || vk_instance == VK_NULL_HANDLE)
        {
            return;
        }

        const auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(vk_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_messenger)
        {
            destroy_messenger(vk_instance, vk_debug_messenger, nullptr);
        }
        else
        {
            TOY_LOG_ERROR("vkDestroyDebugUtilsMessengerEXT is unavailable during Vulkan shutdown.");
        }
        vk_debug_messenger = VK_NULL_HANDLE;
    }

    RHIStatus VulkanDevice::create_primary_surface(const RHISurfaceDesc& desc)
    {
#if WITH_WIN64
        if (desc.platform != RHISurfacePlatform::Win32)
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported, "Vulkan device currently requires a Win32 surface.");
        }

        VkWin32SurfaceCreateInfoKHR create_info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        create_info.hinstance = reinterpret_cast<HINSTANCE>(desc.application_handle);
        create_info.hwnd = reinterpret_cast<HWND>(desc.window_handle);
        return make_vulkan_status(vkCreateWin32SurfaceKHR(vk_instance, &create_info, nullptr, &primary_surface), "vkCreateWin32SurfaceKHR");
#elif WITH_MAC
        if (desc.platform != RHISurfacePlatform::Glfw)
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "The macOS Vulkan backend requires a GLFW-backed surface.");
        }
        auto* glfw_window = static_cast<GLFWwindow*>(desc.window_handle);
        const VkResult result = glfwCreateWindowSurface(
            vk_instance,
            glfw_window,
            nullptr,
            &primary_surface);
        if (result != VK_SUCCESS)
        {
            const char* glfw_error = nullptr;
            glfwGetError(&glfw_error);
            const std::string operation = glfw_error
                ? std::string("glfwCreateWindowSurface: ") + glfw_error
                : std::string("glfwCreateWindowSurface");
            return make_vulkan_status(result, operation.c_str());
        }
        return RHIStatus::success();
#else
        (void)desc;
        return RHIStatus::failure(RHIErrorCode::Unsupported, "Vulkan device does not support this platform.");
#endif
    }

    RHIStatus VulkanDevice::select_physical_device()
    {
        std::uint32_t device_count = 0;
        VkResult result = vkEnumeratePhysicalDevices(vk_instance, &device_count, nullptr);
        if (result != VK_SUCCESS)
        {
            return make_vulkan_status(result, "vkEnumeratePhysicalDevices");
        }
        if (device_count == 0)
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported, "No Vulkan physical device is available.");
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        result = vkEnumeratePhysicalDevices(vk_instance, &device_count, devices.data());
        if (result != VK_SUCCESS)
        {
            return make_vulkan_status(result, "vkEnumeratePhysicalDevices");
        }

        int best_score = -1;
        for (VkPhysicalDevice candidate : devices)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_1)
            {
                continue;
            }
            if (!has_device_extension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            {
                continue;
            }

            std::uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, nullptr);
            std::vector<VkQueueFamilyProperties> queue_properties(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, queue_properties.data());

            for (std::uint32_t index = 0; index < queue_count; ++index)
            {
                VkBool32 supports_present = VK_FALSE;
                result = vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, primary_surface, &supports_present);
                if (result != VK_SUCCESS)
                {
                    return make_vulkan_status(result, "vkGetPhysicalDeviceSurfaceSupportKHR");
                }
                if ((queue_properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 || !supports_present)
                {
                    continue;
                }

                const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if (score > best_score)
                {
                    best_score = score;
                    vk_physical_device = candidate;
                    graphics_queue_family = index;
                }
                break;
            }
        }

        if (vk_physical_device == VK_NULL_HANDLE)
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "No Vulkan device provides one queue family with graphics and present support.");
        }
        return RHIStatus::success();
    }

    RHIStatus VulkanDevice::create_logical_device()
    {
        VkPhysicalDeviceFeatures available_features{};
        vkGetPhysicalDeviceFeatures(vk_physical_device, &available_features);

        const float queue_priority = 1.0F;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = graphics_queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &queue_priority;

        VkPhysicalDeviceFeatures enabled_features{};
        enabled_features.geometryShader = available_features.geometryShader;
        enabled_features.tessellationShader = available_features.tessellationShader;
        enabled_features.samplerAnisotropy = available_features.samplerAnisotropy;
        enabled_features.fragmentStoresAndAtomics = available_features.fragmentStoresAndAtomics;

        std::vector<const char*> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#if WITH_MAC
        if (has_device_extension(vk_physical_device, portability_subset_extension_name))
        {
            extensions.push_back(portability_subset_extension_name);
        }
#endif
        VkDeviceCreateInfo create_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        create_info.queueCreateInfoCount = 1;
        create_info.pQueueCreateInfos = &queue_info;
        create_info.pEnabledFeatures = &enabled_features;
        create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();

        const RHIStatus status = make_vulkan_status(vkCreateDevice(vk_physical_device, &create_info, nullptr, &vk_device), "vkCreateDevice");
        if (!status)
        {
            return status;
        }
        vkGetDeviceQueue(vk_device, graphics_queue_family, 0, &vk_graphics_queue);
        if (vk_graphics_queue == VK_NULL_HANDLE)
        {
            return RHIStatus::failure(RHIErrorCode::BackendFailure, "vkGetDeviceQueue returned a null graphics queue.");
        }
        return RHIStatus::success();
    }

    void VulkanDevice::query_capabilities_and_limits()
    {
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceProperties(vk_physical_device, &properties);
        vkGetPhysicalDeviceFeatures(vk_physical_device, &features);

        device_capabilities.compute_dispatch = true;
        device_capabilities.storage_resources = features.fragmentStoresAndAtomics == VK_TRUE;
        device_capabilities.indirect_draw = true;
        device_capabilities.geometry_shader = features.geometryShader == VK_TRUE;
        device_capabilities.tessellation_shader = features.tessellationShader == VK_TRUE;
        device_capabilities.timestamp_queries = properties.limits.timestampComputeAndGraphics == VK_TRUE;
        device_capabilities.async_compute_queue = false;
        device_capabilities.parallel_command_recording = false;

        device_limits.max_color_attachments = properties.limits.maxColorAttachments;
        device_limits.max_vertex_buffers = properties.limits.maxVertexInputBindings;
        device_limits.max_texture_dimension_2d = properties.limits.maxImageDimension2D;
        device_limits.max_texture_array_layers = properties.limits.maxImageArrayLayers;
        device_limits.max_uniform_buffer_size = properties.limits.maxUniformBufferRange;
        device_limits.max_binding_slots_per_group = properties.limits.maxPerStageDescriptorUniformBuffers;
        device_limits.max_sampler_anisotropy = features.samplerAnisotropy
            ? static_cast<std::uint32_t>(properties.limits.maxSamplerAnisotropy)
            : 1U;
        device_limits.uniform_buffer_offset_alignment = properties.limits.minUniformBufferOffsetAlignment;
        device_limits.storage_buffer_offset_alignment = properties.limits.minStorageBufferOffsetAlignment;
        device_limits.texture_upload_alignment = properties.limits.optimalBufferCopyOffsetAlignment;
    }

    RHIResult<std::unique_ptr<RHIDevice>> create_vulkan_device()
    {
        return RHIResult<std::unique_ptr<RHIDevice>>::success(std::make_unique<VulkanDevice>());
    }
}
