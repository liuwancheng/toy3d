#pragma once
#include "platform/window_interface.h"
#include <windows.h>

namespace toy3d
{
    class Win32Window : public IWindow
    {
    public:
        Win32Window(HINSTANCE hInstance = nullptr);
        ~Win32Window() final;
        
        bool should_close() final;
        void process_events() final;
        void close() final;
        void resize(uint32_t width, uint32_t height) final;
        
        HWND get_native_hwnd() const{ return hWnd_;}
        HINSTANCE get_native_hinstance() const { return hInstance_; }
    private:
        void create_window();
        void destroy_window();
    private:
        HWND hWnd_ = nullptr;
        HINSTANCE hInstance_ = nullptr;
        bool should_close_ = false;
    };
} // namespace toy3d