/// Mac的窗口使用glfw库处理
#pragma once

#include "platform/window_interface.h"
#include <GLFW/glfw3.h>

namespace toy3d
{
    class MacWindow : public IWindow
    {
      public:
        MacWindow();
        ~MacWindow() final;

        void resize(uint32_t _width, uint32_t _height) final;
        Extent get_display_size() const final;
        Extent get_framebuffer_size() const final;
        bool should_close() final;
        void process_events() final;
        void close() final;

        GLFWwindow* get_glfw_window() const { return glfw_window; }
        void* get_metal_layer() const { return metal_layer; }

      private:
        bool create_glfw_window();
        void destroy_glfw_window();

      private:
        GLFWwindow* glfw_window = nullptr;
        // The Cocoa content view retains this platform presentation layer. The
        // Renderer releases its surface before MacWindow destroys the view.
        void* metal_layer = nullptr;
    };
} // namespace toy3d
