/// Mac的窗口使用glfw库处理
#pragma once

#include "rhi/vulkan/vk_com.h"
#include "window.h"

namespace toy3d
{
    class MacWindow : public IWindow
    {
    public:
        MacWindow(uint32_t width, uint32_t height);
        ~MacWindow() final;

        void resize(uint32_t width, uint32_t height) final;

        bool should_close() final;

        void process_events() final;

        void close() final;
        // 子类特有接口
    public:
        void get_reequired_extensions(const char** ex_name_list, uint32_t* count);

        void create_window_surface(VkInstance instance, VkSurfaceKHR & surface);
    private:
        GLFWwindow* m_glfw_win_handle;
    };
}