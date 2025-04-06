/// Mac的窗口使用glfw库处理
#pragma once

#include "vulkan/vk_com.h"

namespace toy3d
{
    class SurfaceUtils
    {
    public:
        static void get_reequired_extensions(const char** ex_name_list, uint32_t* count);

        static void create_window_surface(VkInstance instance, VkSurfaceKHR & surface);
    };
}