#pragma once

#include <cstdint>

namespace toy3d
{
    enum class EndPlayReason
    {
        Destroyed,
        WorldEndPlay
    };

    enum class WorldLifecycleState
    {
        Created,
        Initialized,
        Playing
    };

    struct WorldTickContext
    {
        double delta_seconds = 0.0;
        double world_time_seconds = 0.0;
        std::uint64_t frame_number = 0;
    };
}
