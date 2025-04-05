#pragma once
#include "platform_input_interface.h"
#include <windows.h>

namespace toy3d
{
    class Win32PlatformInput : public IPlatformInput 
    {
    public:        
        bool init() override;
        void exit() override;

        void update() override;

        void process_win32_msg(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    private:
        HWND hWnd_= nullptr;
        std::map<int, KeyCode> win2keycode;
    };
} // namespace toy3d