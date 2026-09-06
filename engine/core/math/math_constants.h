#pragma once

#include <limits>

namespace toy3d
{
    // C++17 inline variables keep header-defined constants ODR-safe while
    // making one immutable value available to every Toy3dMath caller.
    inline constexpr float k_positive_infinity = std::numeric_limits<float>::infinity();
    inline constexpr float k_negative_infinity = -k_positive_infinity;
    inline constexpr float k_pi = 3.14159265358979323846f;
    inline constexpr float k_inverse_pi = 1.0f / k_pi;
    inline constexpr float k_two_pi = 2.0f * k_pi;
    inline constexpr float k_half_pi = 0.5f * k_pi;
    inline constexpr float k_degrees_to_radians = k_pi / 180.0f;
    inline constexpr float k_radians_to_degrees = 180.0f / k_pi;
    inline constexpr float k_default_float_tolerance = 1.0e-6f;
    inline constexpr float k_normalization_tolerance_squared = 1.0e-12f;
    inline constexpr float k_matrix_inverse_tolerance = 1.0e-8f;

    // These names preserve existing runtime callers during the staged
    // migration. New code must use the purpose-specific k_* constants.
    inline constexpr float POSITIVE_INF = k_positive_infinity;
    inline constexpr float NEGTIVE_INF = k_negative_infinity;
    inline constexpr float PI = k_pi;
    inline constexpr float INVIRSE_PI = k_inverse_pi;
    inline constexpr float TWO_PI = k_two_pi;
    inline constexpr float HALF_PI = k_half_pi;
    inline constexpr float DEG2RAD = k_degrees_to_radians;
    inline constexpr float RAD2DEG = k_radians_to_degrees;
    inline constexpr float EPSILON = k_default_float_tolerance;
} // namespace toy3d
