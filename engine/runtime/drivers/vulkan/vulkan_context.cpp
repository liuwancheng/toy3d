#include "vulkan_context.h"
#include "core/config/config_manager.h"
#include "core/misc/logger.h"

namespace toy3d
{

    // 需要开启的验证层
    static std::vector<const char*> g_enable_layers = {
        "VK_LAYER_KHRONOS_validation",
    };

    VulkanContext::VulkanContext()
    {
    }
    
    VulkanContext::~VulkanContext()
    {
        clear();
    }

    void VulkanContext::clear()
    {
        if(instance != VK_NULL_HANDLE)
        {
            if(surface != VK_NULL_HANDLE)
            {
                vkDestroySurfaceKHR(instance, surface, nullptr);
            }  
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
        if(device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        if(m_swapchain != nullptr)
        {
            m_swapchain.reset();
            m_swapchain = nullptr;
        } 
    }

    void VulkanContext::init()
    {
        bool validation = check_validation_layer_support();
        if(!validation)
        {
            TOY_LOG_ERROR("vulkan init failed... please check validation layers");
            return ;
        }

        create_instance();
        create_surface();
        m_swapchain = std::make_unique<VulkanSwapChain>(this);
        auto& images = m_swapchain.get()->get_images();
        // init frame data
        for(int i = 0; i < images.size(); i++)
        {
            m_frames.push_back(std::make_unique<SwapFrameData>(this, images[i]));
        }
    }

    void VulkanContext::begin_frame()
    {
        // acquire draw image
        const SwapFrameData& prev_frame = get_active_frame();
        VkResult res =  m_swapchain->acquire_next_image(m_active_frame_index, prev_frame.semaphore, VK_NULL_HANDLE);
        if(res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // TODO
            // 如果窗口大小变更，我猜测会进来。那么应该要重新初始化swapchain
            res = m_swapchain->acquire_next_image(m_active_frame_index, prev_frame.semaphore, VK_NULL_HANDLE);
        }
        if(res != VK_SUCCESS)
        {
            vkQueueWaitIdle(graphics_queue);
            return;
        }

        // image get success, wait prev frame render finish
        const SwapFrameData& cur_frame = get_active_frame();
        cur_frame.wait_prev_frame();
    }

    void VulkanContext::submit(const std::vector<VkCommandBuffer>& commands, VkPipelineStageFlags flag)
    {
        // push cmd in queue
        auto& cur_frame = get_active_frame();
        VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = (uint32_t)commands.size();
        submit_info.pCommandBuffers = commands.data();
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &cur_frame.semaphore;
        VkPipelineStageFlags mask{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submit_info.pWaitDstStageMask = &mask;
        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, cur_frame.fence));
    }

    void VulkanContext::submit()
    {
        auto& cur_frame = get_active_frame();
        VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cur_frame.cmd_buffer;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &cur_frame.semaphore;
        VkPipelineStageFlags mask{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submit_info.pWaitDstStageMask = &mask;
        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, cur_frame.fence));
    }

    void VulkanContext::end_frame()
    {
        // present queue
        auto& cur_frame = get_active_frame();
        VkSwapchainKHR swap_chain = m_swapchain->get_handle();
        VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swap_chain;
        present_info.pImageIndices = &m_active_frame_index;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &cur_frame.semaphore;
        VK_CHECK(vkQueuePresentKHR(graphics_queue, &present_info));
    }

    bool VulkanContext::check_validation_layer_support()
    {
        // 获取当前支持的验证层
        std::vector<VkLayerProperties> support_layers;
        uint32_t count;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        support_layers.resize(count);
        vkEnumerateInstanceLayerProperties(&count, support_layers.data());

        for(const char* cur_layer : g_enable_layers)
        {
            bool is_found = false;
            for(const VkLayerProperties& support_layer: support_layers)
            {
                if(strcmp(support_layer.layerName, cur_layer) == 0)
                {
                    is_found = true;
                    break;
                }
            }
            if(!is_found)
            {
                return false;
            }
        }
        return true;
    }

    void VulkanContext::create_instance()
    {
        // 获取所有instance的扩展属性
        uint32_t count;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        m_instance_ex_props.resize(count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, m_instance_ex_props.data());

        for(const VkExtensionProperties & val : m_instance_ex_props)
        {
            TOY_LOG_INFO("\t{}", val.extensionName);
        }

        auto title = ConfigManager::get_instance().get_str("window_title", "toy3d");

        VkApplicationInfo info{};
        info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        info.pNext = nullptr;
        info.apiVersion = VK_MAKE_API_VERSION(0, VK_REQUIRED_VERSION_MAJOR, VK_REQUIRED_VERSION_MINOR, 0);
        info.applicationVersion = VK_MAKE_VERSION(1,0,0);
        info.engineVersion = VK_MAKE_VERSION(1,0,0);
        info.pApplicationName = title.c_str();
        info.pEngineName = title.c_str();

        VkInstanceCreateInfo ins_info{};
        ins_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ins_info.pNext = nullptr;
        ins_info.pApplicationInfo = &info;

        uint32_t ex_count;
        const char** ex_name_list = SurfaceUtils::get_required_extensions(&ex_count);
        ins_info.enabledExtensionCount = ex_count;
        ins_info.ppEnabledExtensionNames = ex_name_list;
        ins_info.enabledLayerCount = static_cast<uint32_t>(g_enable_layers.size());
        ins_info.ppEnabledLayerNames = g_enable_layers.data();


        VK_CHECK(vkCreateInstance(&ins_info, nullptr, &instance));
    }

    void VulkanContext::create_surface()
    {
        SurfaceUtils::create_window_surface(instance, surface);
    }

    bool VulkanContext::select_physical_device()
    {
        uint32_t count;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if(count == 0)
        {
            TOY_LOG_ERROR(" no physical device, please check your gpu card ");
            return false;
        }
        m_physical_devices.resize(count);
        vkEnumeratePhysicalDevices(instance, &count, m_physical_devices.data());

        std::map<uint32_t, std::string> gpu_info;
        gpu_info[0x1002]= "AMD";
        gpu_info[0x1010] = "ImgTec";
        gpu_info[0x10DE] = "NVIDIA";
        gpu_info[0x13B5] = "ARM";
        gpu_info[0x106B] = "APPLE";
        gpu_info[0x5143] = "Qualcomm";
        gpu_info[0x8086] = "INTEL";

        for(const VkPhysicalDevice & physical_device : m_physical_devices)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(physical_device, &properties);
            if(properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                continue;
            }
            const int major = VK_API_VERSION_MAJOR(properties.apiVersion);
            const int minor = VK_API_VERSION_MINOR(properties.apiVersion);
            // Does the device support the required Vulkan level?
            if (major < VK_REQUIRED_VERSION_MAJOR) 
            {
                continue;
            }
            if (major == VK_REQUIRED_VERSION_MAJOR && minor < VK_REQUIRED_VERSION_MINOR) 
            {
                continue;
            }

            uint32_t ex_count = 0;
            vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &ex_count, nullptr);
            std::vector<VkExtensionProperties> extensions;
            vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &ex_count, extensions.data());
            bool support_swapchain = false;
            for (uint32_t k = 0; k < ex_count; ++k) 
            {
                if (!strcmp(extensions[k].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                    support_swapchain = true;
                } else if (!strcmp(extensions[k].extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) {
                    b_support_debug_markers = true;
                } else if (!strcmp(extensions[k].extensionName, VK_KHR_MAINTENANCE1_EXTENSION_NAME)) {
                    b_support_maintenance[0] = true;
                } else if (!strcmp(extensions[k].extensionName, VK_KHR_MAINTENANCE2_EXTENSION_NAME)) {
                    b_support_maintenance[1] = true;
                } else if (!strcmp(extensions[k].extensionName, VK_KHR_MAINTENANCE3_EXTENSION_NAME)) {
                    b_support_maintenance[2] = true;
                }
            }
            if (!support_swapchain) 
                continue;

            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
            m_queue_family_props.resize(count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, m_queue_family_props.data());
            graphics_family_index = get_queue_family_index(VK_QUEUE_GRAPHICS_BIT);

            vkGetPhysicalDeviceFeatures(physical_device, &m_physical_device_features);
            vkGetPhysicalDeviceMemoryProperties(physical_device, &m_physical_devie_memory_properties);

            gpu_device = physical_device;

            // Print out some properties of the GPU for diagnostic purposes.
            //
            // Ideally, the vendors register their vendor ID's with Khronos so that apps can make an
            // id => string mapping. However, in practice this hasn't happened. At the time of this
            // writing the gpuinfo database has the following ID's:
            //
            //     0x1002 - AMD
            //     0x1010 - ImgTec
            //     0x10DE - NVIDIA
            //     0x13B5 - ARM
            //     0x106B - APPLE
            //     0x5143 - Qualcomm
            //     0x8086 - INTEL
            //
            // Since we don't have any vendor-specific workarounds yet, there's no need to make this
            // mapping in code. The "deviceName" string informally reveals the marketing name for the
            // GPU. (e.g., Quadro)
            const uint32_t driver_version = properties.driverVersion;
            const uint32_t vendor_id = properties.vendorID;
            const uint32_t device_id = properties.deviceID;
            const std::string gpu_name = gpu_info.count(vendor_id) ? gpu_info[vendor_id] : std::to_string(vendor_id);
            TOY_LOG_INFO("Selected physical device '{}' from {} physical devices. (vendor {}, device {}, driver {}, api {}.{})"
                ,properties.deviceName
                ,count
                ,gpu_name
                ,device_id
                ,driver_version
                ,major, minor);
            return true;
        }

        return false;
    }

    void VulkanContext::create_logic_device()
    {
        VkDeviceQueueCreateInfo queue_info;
        float queue_priority = 1.0f;
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = graphics_family_index;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &queue_priority;

        VkPhysicalDeviceFeatures features{};
        features.samplerAnisotropy = m_physical_device_features.samplerAnisotropy;
        features.textureCompressionETC2 = m_physical_device_features.textureCompressionETC2;
        features.imageCubeArray = m_physical_device_features.textureCompressionBC;

        std::vector<const char*> extensions;
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        // if (debugMarkersSupported) {
        //     extensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
        // }
        if (b_support_maintenance[0]) {
            extensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
        }
        if (b_support_maintenance[1]) {
            extensions.push_back(VK_KHR_MAINTENANCE2_EXTENSION_NAME);
        }
        if (b_support_maintenance[2]) {
            extensions.push_back(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
        }

        VkDeviceCreateInfo create_info;
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.pEnabledFeatures = &features;
        create_info.queueCreateInfoCount = 1;
        create_info.pQueueCreateInfos = &queue_info;
        create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();
        create_info.enabledLayerCount = static_cast<uint32_t>(g_enable_layers.size());
        if(g_enable_layers.size())
        {   
            create_info.ppEnabledLayerNames = g_enable_layers.data();
        }
        if (vkCreateDevice(gpu_device, &create_info, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("failed to create logical device!");

        // 获取queue
        vkGetDeviceQueue(device, graphics_family_index, 0, &graphics_queue);

        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 
                            | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = graphics_family_index;

        // command pool
        vkCreateCommandPool(device, &pool_info, nullptr, &graphics_cmd_pool);

        // Create a timestamp pool large enough to hold a pair of queries for each timer.
        VkQueryPoolCreateInfo query_pool_info = {};
        query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;

        std::unique_lock<std::mutex> timestamps_lock(query_pool._mutex);
        query_pool_info.queryCount = query_pool._bitmap.size();
        vkCreateQueryPool(device, &query_pool_info, nullptr, &query_pool._pool);
        query_pool._bitmap.reset();
        query_pool._mutex.unlock();

        VmaVulkanFunctions funcs = {};
    #if VMA_DYNAMIC_VULKAN_FUNCTIONS
        funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    #else
        funcs.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
        funcs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
        funcs.vkAllocateMemory = vkAllocateMemory;
        funcs.vkFreeMemory = vkFreeMemory;
        funcs.vkMapMemory = vkMapMemory;
        funcs.vkUnmapMemory = vkUnmapMemory;
        funcs.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
        funcs.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
        funcs.vkBindBufferMemory = vkBindBufferMemory;
        funcs.vkBindImageMemory = vkBindImageMemory;
        funcs.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
        funcs.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
        funcs.vkCreateBuffer = vkCreateBuffer;
        funcs.vkDestroyBuffer = vkDestroyBuffer;
        funcs.vkCreateImage = vkCreateImage;
        funcs.vkDestroyImage = vkDestroyImage;
        funcs.vkCmdCopyBuffer = vkCmdCopyBuffer;
        funcs.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR;
        funcs.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR;
    #endif
        VmaAllocatorCreateInfo allocator_info = {};
        allocator_info.physicalDevice = gpu_device;
        allocator_info.device = device;
        allocator_info.pVulkanFunctions = &funcs;
        allocator_info.instance = instance;
        vmaCreateAllocator(&allocator_info, &allocator);
    
        //graphics_cmd_buffer = new VulkanCommands(device, graphics_family_index);
    }

    uint32_t VulkanContext::get_queue_family_index(VkQueueFlagBits queue_flag)
    {
        // 优先找专属队列
        if(queue_flag & VK_QUEUE_COMPUTE_BIT)
        {
            for (size_t i = 0; i < m_queue_family_props.size(); i++)
            {
                if((m_queue_family_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
                && (m_queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
                {
                    return static_cast<uint32_t>(i);
                }
            }
        }

        // 优先找专属队列
        if(queue_flag & VK_QUEUE_TRANSFER_BIT)
        {
            for (size_t i = 0; i < m_queue_family_props.size(); i++)
            {
                if((m_queue_family_props[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
                && (m_queue_family_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0
                && (m_queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
                {
                    return static_cast<uint32_t>(i);
                }
            }
        }

        for (size_t i = 0; i < m_queue_family_props.size(); i++)
        {
            VkBool32 supports_present;
		    vkGetPhysicalDeviceSurfaceSupportKHR(gpu_device, i, surface, &supports_present);
            if(supports_present && (m_queue_family_props[i].queueFlags & queue_flag))
            {
                return static_cast<uint32_t>(i);
            }
        }
        throw std::runtime_error("could not find a matching queue family index");
    }


}