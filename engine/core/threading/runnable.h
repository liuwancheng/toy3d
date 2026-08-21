#pragma once

#include "threading/threading_types.h"

#include <cstdint>

namespace toy3d
{
    class Runnable
    {
    public:
        virtual ~Runnable() = default;

        virtual ThreadStatus init()
        {
            return ThreadStatus::success();
        }

        virtual std::uint32_t run() = 0;

        virtual void stop()
        {
        }

        virtual void exit()
        {
        }
    };
}
