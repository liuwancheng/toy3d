#pragma once
#include "platform/platform_input_interface.h"
#include <windows.h>

namespace toy3d
{
    class Win32PlatformInput : public IPlatformInput 
    {
    public:
        Win32PlatformInput() :IPlatformInput() {};
        virtual ~Win32PlatformInput() {};

        virtual bool init() override;
        virtual void exit() override;

        virtual void update() override;

        void process_win32_msg(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    };
} // namespace toy3d