#pragma once
#include "platform/platform_input_interface.h"

namespace toy3d
{
    class AndroidPlatformInput : public IPlatformInput 
    {
    public:
        AndroidPlatformInput() :IPlatformInput() {};
        virtual ~AndroidPlatformInput() {};

        bool init() override;
        void exit() override;
        
        void update() override;
    };
} // namespace toy3d