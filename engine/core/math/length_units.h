#pragma once

namespace toy3d
{
    // C++17 inline constexpr exposes one immutable conversion constant to all
    // Core Math consumers without requiring a separately defined global value.
    inline constexpr float k_centimeters_per_meter = 100.0f;

    // World positions and lengths are stored in centimeters. These arithmetic
    // helpers do not change the world convention or validate caller inputs.
    constexpr float meters_to_centimeters(float meters)
    {
        return meters * k_centimeters_per_meter;
    }

    constexpr float centimeters_to_meters(float centimeters)
    {
        return centimeters / k_centimeters_per_meter;
    }
} // namespace toy3d
