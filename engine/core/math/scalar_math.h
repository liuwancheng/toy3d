#pragma once

#include "math/math_constants.h"

#include <cmath>

namespace toy3d
{
    inline float abs(float value)
    {
        return std::fabs(value);
    }

    constexpr float min(float left, float right)
    {
        return left < right ? left : right;
    }

    constexpr float max(float left, float right)
    {
        return left > right ? left : right;
    }

    constexpr float clamp(float value, float minimum, float maximum)
    {
        return value < minimum ? minimum : (value > maximum ? maximum : value);
    }

    constexpr float lerp(float start, float end, float alpha)
    {
        return start + (end - start) * alpha;
    }

    constexpr float square(float value)
    {
        return value * value;
    }

    inline float sqrt(float value)
    {
        return std::sqrt(value);
    }

    inline float inverse_sqrt(float value)
    {
        return 1.0f / std::sqrt(value);
    }

    inline bool is_finite(float value)
    {
        return std::isfinite(value);
    }

    inline bool is_finite(double value)
    {
        return std::isfinite(value);
    }

    inline bool is_nearly_zero(float value, float tolerance = k_default_float_tolerance)
    {
        return is_finite(value) && is_finite(tolerance) && tolerance >= 0.0f && abs(value) <= tolerance;
    }

    inline bool is_nearly_equal(float left, float right, float tolerance = k_default_float_tolerance)
    {
        return is_finite(left) && is_finite(right) && is_finite(tolerance) && tolerance >= 0.0f &&
               abs(right - left) <= tolerance;
    }
} // namespace toy3d
