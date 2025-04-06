#pragma once
#include "vk_com.h"
#include "surface/surface_utils.h"
#include "vulkan_swapchain.h"
#include "vulkan_resource.h"

namespace toy3d
{
    struct VkTimeStampsQueryPool
    {
        VkQueryPool _pool;
        std::mutex _mutex;
        std::bitset<32> _bitmap;
    };

    class VulkanContext
    {
    public:
        VulkanContext();
        ~VulkanContext();
    public:
        uint32_t get_queue_family_index(VkQueueFlagBits queue_flag);

        void init();

        void clear();

        void begin_frame();

        void end_frame();

        void submit();

        void submit(const std::vector<VkCommandBuffer>& commands, VkPipelineStageFlags flag);
    public:
        SwapFrameData& get_active_frame(){return *m_frames.at(m_active_frame_index);};

        VkCommandBuffer& get_cmd_buffer(){return m_frames.at(m_active_frame_index).get()->cmd_buffer;};

        VulkanSwapChain& get_swapchain(){return *m_swapchain;};
    private:
        void create_instance();

        void create_surface();

        bool select_physical_device();

        void create_logic_device();

        bool check_validation_layer_support();
    public:
        uint32_t graphics_family_index;
        VkInstance instance{VK_NULL_HANDLE};
        VkSurfaceKHR surface{VK_NULL_HANDLE};
        VkPhysicalDevice gpu_device{VK_NULL_HANDLE};
        VkDevice device{VK_NULL_HANDLE};
        VkQueue graphics_queue{VK_NULL_HANDLE};
    private: 
        VkPhysicalDeviceFeatures m_physical_device_features;
        VkPhysicalDeviceMemoryProperties m_physical_devie_memory_properties;
        std::vector<VkPhysicalDevice> m_physical_devices;
        std::vector<VkQueueFamilyProperties> m_queue_family_props;
        std::vector<VkExtensionProperties> m_instance_ex_props;

        bool b_support_debug_markers;
        bool b_support_maintenance[3];
        VkTimeStampsQueryPool query_pool;
        VkCommandPool graphics_cmd_pool{VK_NULL_HANDLE};
        VmaAllocator allocator{VK_NULL_HANDLE};

    private:
        uint32_t m_active_frame_index{0};
        std::vector<VkSemaphore> m_swapchain_semaphores;
        std::vector<VkFence> m_swapchain_fence;

        std::unique_ptr<VulkanSwapChain> m_swapchain{nullptr};
        std::vector<std::unique_ptr<SwapFrameData>> m_frames;
    };
}