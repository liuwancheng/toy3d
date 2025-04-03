#pragma once
#include "window_interface.h"
#include <string>
#include <memory>

namespace toy3d
{
    class WinWindow : public WindowInterface
    {
    public:
        WinWindow(HINSTANCE hInstance = nullptr);
        ~WinWindow() override;
        
        bool should_close() override;
        void process_events() override;
        void close() override;
        void resize(uint32_t width, uint32_t height) override;
        
        void* get_native_hwnd() const{ return _hWnd;}
    private:
        void create_window();
        void destroy_window();
    private:
        HWND _hWnd = nullptr;
        HINSTANCE _hInstance = nullptr;
        bool _should_close = false;
    };
} // namespace toy3d