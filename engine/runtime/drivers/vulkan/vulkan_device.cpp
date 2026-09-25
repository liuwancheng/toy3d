#include "drivers/vulkan/vulkan_device.h"
#include "drivers/vulkan/vulkan_binding_creation.h"
#include "drivers/vulkan/vulkan_command_context.h"
#include "drivers/vulkan/vulkan_deferred_deletion.h"
#include "drivers/vulkan/vulkan_memory_manager.h"
#include "drivers/vulkan/vulkan_pipeline_creation.h"
#include "drivers/vulkan/vulkan_upload_manager.h"
#include "drivers/vulkan/vulkan_queue.h"
#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_resource_creation.h"
#include "drivers/vulkan/vulkan_type_mapping.h"
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

namespace toy3d
{
    namespace
    {
#if WITH_MAC
        constexpr const char* portability_enumeration_extension_name = "VK_KHR_portability_enumeration";
        constexpr const char* portability_subset_extension_name = "VK_KHR_portability_subset";
        constexpr VkInstanceCreateFlags enumerate_portability_flag = 0x00000001;
#endif

        VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                             VkDebugUtilsMessageTypeFlagsEXT message_types,
                                                             const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                             void*)
        {
            const char* message = callback_data && callback_data->pMessage
                                      ? callback_data->pMessage
                                      : "Vulkan validation emitted an empty message.";
            const char* message_id =
                callback_data && callback_data->pMessageIdName ? callback_data->pMessageIdName : "unknown";

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
            VkDebugUtilsMessengerCreateInfoEXT create_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            create_info.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
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
                return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                          "Failed to query the VK_LAYER_PATH environment variable.");
            }
            if (!SetEnvironmentVariableA("VK_LAYER_PATH", TOY3D_VK_LAYER_PATH))
            {
                return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                          "Failed to configure VK_LAYER_PATH for the bundled validation layer.");
            }
#elif WITH_MAC
            if (std::getenv("VK_ICD_FILENAMES") == nullptr && setenv("VK_ICD_FILENAMES", TOY3D_VK_ICD_PATH, 0) != 0)
            {
                return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                          "Failed to configure VK_ICD_FILENAMES for the bundled MoltenVK ICD.");
            }
            if (enable_validation && std::getenv("VK_LAYER_PATH") == nullptr &&
                setenv("VK_LAYER_PATH", TOY3D_VK_LAYER_PATH, 0) != 0)
            {
                return RHIStatus::failure(RHIErrorCode::BackendFailure,
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
            return RHIStatus::failure(code, std::string(operation) + " failed with VkResult " +
                                                std::to_string(static_cast<int>(result)) + ".");
        }

        bool has_instance_extension(const std::vector<VkExtensionProperties>& extensions, const char* name)
        {
            return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension)
                               { return std::strcmp(extension.extensionName, name) == 0; });
        }

        bool has_device_extension(VkPhysicalDevice physical_device, const char* name)
        {
            std::uint32_t extension_count = 0;
            if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr) != VK_SUCCESS)
            {
                return false;
            }

            std::vector<VkExtensionProperties> extensions(extension_count);
            if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, extensions.data()) !=
                VK_SUCCESS)
            {
                return false;
            }

            return has_instance_extension(extensions, name);
        }

    } // namespace

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
        descriptor_pool_manager_instance = std::make_unique<VulkanDescriptorPoolManager>(vk_device);
        deletion_queue = std::make_unique<VulkanDeferredDeletionQueue>();
        queue = std::make_unique<VulkanQueue>(*this, vk_device, vk_graphics_queue, *upload_manager_instance);
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
        if (descriptor_pool_manager_instance)
            descriptor_pool_manager_instance->shutdown();
        descriptor_pool_manager_instance.reset();
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

    RHIFormatCapabilities VulkanDevice::format_capabilities(PixelFormat format) const
    {
        RHIFormatCapabilities result;
        if (vk_physical_device == VK_NULL_HANDLE)
        {
            return result;
        }

        const VkFormat vk_format = vulkan_format_from_pixel_format(format);
        if (vk_format == VK_FORMAT_UNDEFINED)
        {
            return result;
        }

        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(vk_physical_device, vk_format, &properties);
        const VkFormatFeatureFlags features = properties.optimalTilingFeatures;
        const VkFormatFeatureFlags buffer_features = properties.bufferFeatures;
        if ((features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0)
        {
            result.usage |= RHIFormatUsage::Sampled;
        }
        if ((features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0)
        {
            result.usage |= RHIFormatUsage::Storage;
        }
        if ((features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0)
        {
            result.usage |= RHIFormatUsage::RenderTarget;
        }
        if ((features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        {
            result.usage |= RHIFormatUsage::DepthStencil;
        }
        if ((buffer_features & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0)
        {
            result.usage |= RHIFormatUsage::VertexBuffer;
        }
        if ((features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0)
        {
            result.usage |= RHIFormatUsage::CopySource;
        }
        if ((features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0)
        {
            result.usage |= RHIFormatUsage::CopyDestination;
        }
        result.supported_sample_counts = VK_SAMPLE_COUNT_1_BIT;
        return result;
    }

    RHIQueue& VulkanDevice::graphics_queue()
    {
        return *queue;
    }

    RHIResult<std::unique_ptr<RHIViewportContext>> VulkanDevice::create_viewport_context_impl(
        const RHISurfaceRef& surface, const RHIViewportContextDesc& desc)
    {
        if (surface != primary_rhi_surface || primary_surface == VK_NULL_HANDLE)
        {
            return RHIResult<std::unique_ptr<RHIViewportContext>>::failure(
                RHIErrorCode::Unsupported, "Vulkan viewport contexts currently support only the primary surface.");
        }
        return RHIResult<std::unique_ptr<RHIViewportContext>>::success(std::make_unique<VulkanViewportContext>(
            *this, vk_physical_device, vk_device, primary_surface, graphics_queue_family, *queue,
            *upload_manager_instance, *descriptor_pool_manager_instance, *deletion_queue, surface, desc));
    }

    RHIResult<RHIBufferRef> VulkanDevice::create_buffer_impl(const RHIBufferDesc& desc,
                                                             const RHIInitialData* initial_data)
    {
        return create_vulkan_buffer(*this, vk_device, *memory_manager_instance, *deletion_queue, desc, initial_data);
    }

    RHIResult<RHITextureRef> VulkanDevice::create_texture_impl(const RHITextureDesc& desc,
                                                               const RHIInitialData* initial_data)
    {
        return create_vulkan_texture(*this, vk_physical_device, vk_device, *memory_manager_instance, *deletion_queue,
                                     desc, initial_data);
    }

    RHIResult<RHIReadbackRef> VulkanDevice::create_readback_impl(const std::string& debug_name)
    {
        return create_vulkan_readback(*this, vk_device, *memory_manager_instance, *deletion_queue, debug_name);
    }

    RHIResult<RHIBufferViewRef> VulkanDevice::create_buffer_view_impl(const RHIBufferRef& buffer,
                                                                      const RHIBufferViewDesc& desc)
    {
        return create_vulkan_buffer_view(buffer, desc);
    }

    RHIResult<RHITextureViewRef> VulkanDevice::create_texture_view_impl(const RHITextureRef& texture,
                                                                        const RHITextureViewDesc& desc)
    {
        return create_vulkan_texture_view(vk_device, texture, desc);
    }

    RHIResult<RHIShaderRef> VulkanDevice::create_shader_impl(const RHIShaderDesc& desc)
    {
        return create_vulkan_shader(*this, vk_device, desc);
    }

    RHIResult<RHIBindingLayoutRef> VulkanDevice::create_binding_layout_impl(const RHIBindingLayoutDesc& desc)
    {
        return create_vulkan_binding_layout(*this, vk_device, desc);
    }

    RHIResult<RHISamplerRef> VulkanDevice::create_sampler_impl(const RHISamplerDesc& desc)
    {
        return create_vulkan_sampler(*this, vk_device, desc);
    }

    RHIResult<RHIGraphicsPipelineRef> VulkanDevice::create_graphics_pipeline_impl(const RHIGraphicsPipelineDesc& desc)
    {
        return create_vulkan_graphics_pipeline(*this, vk_device, desc);
    }

    RHIResult<RHIGPUFenceRef> VulkanDevice::create_gpu_fence_impl(const std::string& debug_name)
    {
        return create_vulkan_gpu_fence(debug_name);
    }

    RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> VulkanDevice::create_graphics_command_context_impl()
    {
        return create_vulkan_graphics_command_context(*this, vk_device, graphics_queue_family,
                                                      *upload_manager_instance, *descriptor_pool_manager_instance);
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
        if (descriptor_pool_manager_instance)
        {
            observation.descriptors = descriptor_pool_manager_instance->statistics();
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
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#else
        return RHIStatus::failure(RHIErrorCode::Unsupported, "Vulkan device does not support this platform.");
#endif

        VkInstanceCreateFlags instance_flags = 0;
#if WITH_MAC
        if (has_instance_extension(available_extensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        {
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }
        if (has_instance_extension(available_extensions, portability_enumeration_extension_name))
        {
            extensions.push_back(portability_enumeration_extension_name);
            instance_flags |= enumerate_portability_flag;
        }
#endif
        std::sort(extensions.begin(), extensions.end(),
                  [](const char* lhs, const char* rhs) { return std::strcmp(lhs, rhs) < 0; });
        extensions.erase(std::unique(extensions.begin(), extensions.end(),
                                     [](const char* lhs, const char* rhs) { return std::strcmp(lhs, rhs) == 0; }),
                         extensions.end());
        for (const char* extension : extensions)
        {
            if (!has_instance_extension(available_extensions, extension))
            {
                return RHIStatus::failure(RHIErrorCode::Unsupported,
                                          std::string("Required Vulkan instance extension is unavailable: ") +
                                              extension);
            }
        }

        std::vector<const char*> layers;
        VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
        if (desc.enable_validation)
        {
            constexpr const char* validation_layer = "VK_LAYER_KHRONOS_validation";
            if (!has_instance_extension(available_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            {
                return RHIStatus::failure(RHIErrorCode::Unsupported,
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
            const bool layer_found =
                std::any_of(available_layers.begin(), available_layers.end(), [](const VkLayerProperties& layer)
                            { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
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

        RHIStatus status =
            make_vulkan_status(vkCreateInstance(&create_info, nullptr, &vk_instance), "vkCreateInstance");
        if (!status || !desc.enable_validation)
        {
            return status;
        }
        status = create_debug_messenger();
        if (status)
        {
            TOY_LOG_INFO("Vulkan validation layer enabled.");
        }
        return status;
    }

    RHIStatus VulkanDevice::create_debug_messenger()
    {
        const auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(vk_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!create_messenger)
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported, "vkCreateDebugUtilsMessengerEXT is unavailable.");
        }

        const VkDebugUtilsMessengerCreateInfoEXT create_info = make_debug_messenger_create_info();
        return make_vulkan_status(create_messenger(vk_instance, &create_info, nullptr, &vk_debug_messenger),
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
        return make_vulkan_status(vkCreateWin32SurfaceKHR(vk_instance, &create_info, nullptr, &primary_surface),
                                  "vkCreateWin32SurfaceKHR");
#elif WITH_MAC
        if (desc.platform != RHISurfacePlatform::MacOS)
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported,
                                      "The macOS Vulkan backend requires a main-thread Metal presentation layer.");
        }
        VkMetalSurfaceCreateInfoEXT create_info{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
        create_info.pLayer = static_cast<const CAMetalLayer*>(desc.window_handle);
        return make_vulkan_status(vkCreateMetalSurfaceEXT(vk_instance, &create_info, nullptr, &primary_surface),
                                  "vkCreateMetalSurfaceEXT");
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
            return RHIStatus::failure(RHIErrorCode::Unsupported,
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
        create_info.pNext = nullptr;

        const VkResult create_result = vkCreateDevice(vk_physical_device, &create_info, nullptr, &vk_device);
        const RHIStatus status = make_vulkan_status(create_result, "vkCreateDevice");
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
        device_limits.max_dynamic_uniform_buffers = properties.limits.maxDescriptorSetUniformBuffersDynamic;
        device_limits.max_sampler_anisotropy =
            features.samplerAnisotropy ? static_cast<std::uint32_t>(properties.limits.maxSamplerAnisotropy) : 1U;
        device_limits.uniform_buffer_offset_alignment = properties.limits.minUniformBufferOffsetAlignment;
        device_limits.storage_buffer_offset_alignment = properties.limits.minStorageBufferOffsetAlignment;
        device_limits.texture_upload_alignment = properties.limits.optimalBufferCopyOffsetAlignment;
    }

    RHIResult<std::unique_ptr<RHIDevice>> create_vulkan_device()
    {
        return RHIResult<std::unique_ptr<RHIDevice>>::success(std::make_unique<VulkanDevice>());
    }
} // namespace toy3d
