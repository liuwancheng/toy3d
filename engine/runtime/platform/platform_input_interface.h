#pragma once
#include "core/misc/pch.h"

namespace toy3d
{
    class IPlatformInput 
    {
    public:
		IPlatformInput() {};
		virtual ~IPlatformInput() {};

        virtual bool init() = 0;
        virtual void exit() = 0;

        virtual void update() = 0;
    };
} // namespace toy3d