#pragma once
#include "runtime_pch.h"

namespace toy3d
{
    struct PlatformInputCapabilities
    {
        bool mouse = false;
        bool keyboard = false;
        bool text = false;
        bool focus = false;
        bool framebuffer_scale = false;

        bool supports_interactive_imgui() const noexcept
        {
            return mouse && keyboard && text && focus && framebuffer_scale;
        }
    };

    class IPlatformInput 
    {
    public:
		IPlatformInput() {};
		virtual ~IPlatformInput() {};

        virtual bool init() = 0;
        virtual void exit() = 0;

        virtual void update() = 0;
        virtual PlatformInputCapabilities capabilities() const noexcept = 0;
    };
} // namespace toy3d
