#pragma once

#include "math/scalar_math.h"

#include <cmath>

namespace toy3d
{
    class Radians
    {
      public:
        constexpr Radians() = default;
        explicit constexpr Radians(float value) : value_(value)
        {
        }

        constexpr float value() const
        {
            return value_;
        }

        constexpr Radians operator+() const
        {
            return *this;
        }

        constexpr Radians operator-() const
        {
            return Radians(-value_);
        }

        constexpr Radians& operator+=(Radians other)
        {
            value_ += other.value_;
            return *this;
        }

        constexpr Radians& operator-=(Radians other)
        {
            value_ -= other.value_;
            return *this;
        }

        constexpr Radians& operator*=(float scalar)
        {
            value_ *= scalar;
            return *this;
        }

        constexpr Radians& operator/=(float scalar)
        {
            value_ /= scalar;
            return *this;
        }

      private:
        float value_ = 0.0f;
    };

    class Degrees
    {
      public:
        constexpr Degrees() = default;
        explicit constexpr Degrees(float value) : value_(value)
        {
        }

        constexpr float value() const
        {
            return value_;
        }

        constexpr Degrees operator+() const
        {
            return *this;
        }

        constexpr Degrees operator-() const
        {
            return Degrees(-value_);
        }

        constexpr Degrees& operator+=(Degrees other)
        {
            value_ += other.value_;
            return *this;
        }

        constexpr Degrees& operator-=(Degrees other)
        {
            value_ -= other.value_;
            return *this;
        }

        constexpr Degrees& operator*=(float scalar)
        {
            value_ *= scalar;
            return *this;
        }

        constexpr Degrees& operator/=(float scalar)
        {
            value_ /= scalar;
            return *this;
        }

      private:
        float value_ = 0.0f;
    };

    constexpr Radians operator+(Radians left, Radians right)
    {
        return left += right;
    }

    constexpr Radians operator-(Radians left, Radians right)
    {
        return left -= right;
    }

    constexpr Radians operator*(Radians angle, float scalar)
    {
        return angle *= scalar;
    }

    constexpr Radians operator*(float scalar, Radians angle)
    {
        return angle *= scalar;
    }

    constexpr Radians operator/(Radians angle, float scalar)
    {
        return angle /= scalar;
    }

    constexpr Degrees operator+(Degrees left, Degrees right)
    {
        return left += right;
    }

    constexpr Degrees operator-(Degrees left, Degrees right)
    {
        return left -= right;
    }

    constexpr Degrees operator*(Degrees angle, float scalar)
    {
        return angle *= scalar;
    }

    constexpr Degrees operator*(float scalar, Degrees angle)
    {
        return angle *= scalar;
    }

    constexpr Degrees operator/(Degrees angle, float scalar)
    {
        return angle /= scalar;
    }

    constexpr bool operator==(Radians left, Radians right)
    {
        return left.value() == right.value();
    }

    constexpr bool operator!=(Radians left, Radians right)
    {
        return !(left == right);
    }

    constexpr bool operator==(Degrees left, Degrees right)
    {
        return left.value() == right.value();
    }

    constexpr bool operator!=(Degrees left, Degrees right)
    {
        return !(left == right);
    }

    constexpr Radians to_radians(Degrees value)
    {
        return Radians(value.value() * k_degrees_to_radians);
    }

    constexpr Degrees to_degrees(Radians value)
    {
        return Degrees(value.value() * k_radians_to_degrees);
    }

    inline bool is_finite(Radians value)
    {
        return is_finite(value.value());
    }

    inline bool is_finite(Degrees value)
    {
        return is_finite(value.value());
    }

    inline bool is_nearly_equal(Radians left, Radians right, float tolerance = k_default_float_tolerance)
    {
        return is_nearly_equal(left.value(), right.value(), tolerance);
    }

    inline bool is_nearly_equal(Degrees left, Degrees right, float tolerance = k_default_float_tolerance)
    {
        return is_nearly_equal(left.value(), right.value(), tolerance);
    }

    inline float sin(Radians value)
    {
        return std::sin(value.value());
    }

    inline float cos(Radians value)
    {
        return std::cos(value.value());
    }

    inline float tan(Radians value)
    {
        return std::tan(value.value());
    }
} // namespace toy3d
