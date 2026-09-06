#pragma once
#include "platform/window_interface.h"
#include <windows.h>

namespace toy3d
{
    class Win32Window : public IWindow
    {
      public:
        explicit Win32Window(HINSTANCE instance = nullptr);
        ~Win32Window() final;

        bool should_close() final;
        void process_events() final;
        void close() final;
        void resize(uint32_t _width, uint32_t _height) final;
        Extent get_display_size() const final;
        Extent get_framebuffer_size() const final;

        HWND get_native_hwnd() const { return hWnd; }
        HINSTANCE get_native_hinstance() const { return hInstance; }

      private:
        void create_window();
        void destroy_window();

      private:
        HWND hWnd = nullptr;
        HINSTANCE hInstance = nullptr;
        bool b_close = false;
    };
} // namespace toy3d
