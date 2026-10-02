#pragma once
#include "platform/platform_interface.h"

namespace toy3d
{
    class AndroidPlatform : public IPlatform
    {
      public:
        AndroidPlatform() : IPlatform() {};
        virtual ~AndroidPlatform() {};

        virtual bool init();
        virtual void exit();
        virtual const char* get_platform_name() const
        {
            return "AndroidPlatform";
        };
    };
} // namespace toy3d