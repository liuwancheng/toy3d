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
        PlatformInputCapabilities capabilities() const noexcept override
        {
            return {true, true, true, true, true};
        }

        void process_win32_msg(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    private:
        wchar_t pending_high_surrogate_ = 0;
    };
} // namespace toy3d
