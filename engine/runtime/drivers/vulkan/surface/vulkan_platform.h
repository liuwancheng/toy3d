/// Mac的窗口使用glfw库处理
#pragma once

#include "vulkan/vk_com.h"

namespace toy3d
{
    class VulkanGenericPlatform
    {
    public:
        static const char** get_required_extensions(uint32_t* count);

        static void create_window_surface(VkInstance instance, VkSurfaceKHR & surface);
    };
}