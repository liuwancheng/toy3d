#pragma once

#include "platform/window_interface.h"
#include <GLFW/glfw3.h>

namespace toy3d
{
    class AndroidWindow : public IWindow
    {
    public:
        AndroidWindow();
        ~AndroidWindow();

        void resize(uint32_t width, uint32_t height) final;
        bool should_close() final;
        void process_events() final;
        void close() final;

        GLFWwindow* get_glfw_window(){return glfw_window_;}
    private:
        void create_glfw_window();
        void destroy_glfw_window();
    private:
        GLFWwindow* glfw_window_ = nullptr;
    };
}