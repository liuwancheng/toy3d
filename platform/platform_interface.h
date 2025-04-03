#pragma once
#include "pch.h"

namespace toy3d
{
    class PlatformInterface
    {
    public:
        PlatformInterface(){};
        virtual ~PlatformInterface(){};

        virtual bool init() = 0;
        virtual void exit() = 0;
        virtual const char* get_platform_name() const = 0;
    };
} // namespace toy3d