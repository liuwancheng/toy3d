#pragma once
#include "runtime_pch.h"

namespace toy3d
{
    class IPlatform
    {
    public:
        IPlatform(){};
        virtual ~IPlatform(){};

        virtual bool init() = 0;
        virtual void exit() = 0;
        virtual const char* get_platform_name() const = 0;
    };
} // namespace toy3d
