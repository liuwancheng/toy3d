#pragma once
#include "pch.h"
#include "input_device.h"

namespace toy3d
{
    class IPlatformInput 
    {
    public:
        virtual bool init() = 0;
        virtual void exit() = 0;

        virtual void update() = 0;

        virtual bool is_pressed(KeyCode key_code) const = 0;
    };
} // namespace toy3d