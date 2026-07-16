#pragma once
#include "platform/platform_interface.h"

namespace toy3d
{
    class MacPlatform : public IPlatform
    {
    public:
        MacPlatform() :IPlatform() {};
        virtual ~MacPlatform() {};

        virtual bool init();
        virtual void exit();
        virtual const char* get_platform_name() const { return "MacPlatform"; };
    };
} // namespace toy3d
