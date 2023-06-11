#include "math.h"

namespace toy3d
{
    Math::AngleUnit Math::k_AngleUnit;

    Math::Math() { k_AngleUnit = AngleUnit::AU_DEGREE; }

    bool Math::equal(float a, float b, float tolerance /* = std::numeric_limits<float>::epsilon() */)
    {
        return std::fabs(b - a) <= tolerance;
    }

    float Math::deg2rad(float degrees) { return degrees * DEG2RAD; }

    float Math::rad2deg(float radians) { return radians * RAD2DEG; }

    float Math::ang2rad(float angleunits)
    {
        if (k_AngleUnit == AngleUnit::AU_DEGREE)
            return angleunits * DEG2RAD;

        return angleunits;
    }

    float Math::rad2ang(float radians)
    {
        if (k_AngleUnit == AngleUnit::AU_DEGREE)
            return radians * RAD2DEG;

        return radians;
    }

    float Math::ang2deg(float angleunits)
    {
        if (k_AngleUnit == AngleUnit::AU_RADIAN)
            return angleunits * RAD2DEG;

        return angleunits;
    }

    float Math::deg2ang(float degrees)
    {
        if (k_AngleUnit == AngleUnit::AU_RADIAN)
            return degrees * DEG2RAD;

        return degrees;
    }

    Radian Math::acos(float value)
    {
        if (-1.0 < value)
        {
            if (value < 1.0)
                return Radian(::acos(value));

            return Radian(0.0);
        }

        return Radian(PI);
    }

    Radian Math::asin(float value)
    {
        if (-1.0 < value)
        {
            if (value < 1.0)
                return Radian(::asin(value));

            return Radian(HALF_PI);
        }

        return Radian(-HALF_PI);
    }

} // namespace toy3d
