#pragma once
#include "window_interface.h"
#include <windows.h>

namespace toy3d
{
    class Win32Window : public IWindow
    {
    public:
        Win32Window(HINSTANCE hInstance = nullptr);
        ~Win32Window() override;
        
        bool should_close() override;
        void process_events() override;
        void close() override;
        void resize(uint32_t width, uint32_t height) override;
        
        void* get_native_hwnd() const{ return hWnd_;}
    private:
        void create_window();
        void destroy_window();
    private:
        HWND hWnd_ = nullptr;
        HINSTANCE hInstance_ = nullptr;
        bool should_close_ = false;
    };
} // namespace toy3d