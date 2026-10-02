#pragma once
#include "platform/platform_interface.h"

namespace toy3d
{
    class Win32Platform : public IPlatform
    {
      public:
        Win32Platform() : IPlatform() {};
        virtual ~Win32Platform() {};

        virtual bool init() override;
        virtual void exit() override;
        virtual const char* get_platform_name() const
        {
            return "WindowsPlatform";
        };
    };
} // namespace toy3d