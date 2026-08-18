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

        void resize(uint32_t _width, uint32_t _height) final;
        bool should_close() final;
        void process_events() final;
        void close() final;

        GLFWwindow* get_glfw_window(){return glfw_window;}
    private:
        void create_glfw_window();
        void destroy_glfw_window();
    private:
        GLFWwindow* glfw_window = nullptr;
    };
}
