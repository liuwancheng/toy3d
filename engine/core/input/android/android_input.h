#pragma once
#include "platform_input_interface.h"

namespace toy3d
{
    class AndroidPlatformInput : public IPlatformInput 
    {
    public:
        bool init() override;
        void exit() override;
        void process_message() override;
    };
} // namespace toy3d