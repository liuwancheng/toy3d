#ifndef TOY3D_LIGHTING_INCLUDED
#define TOY3D_LIGHTING_INCLUDED

float3 toy_safe_normalize(float3 value, float3 fallback)
{
    const float length_squared = dot(value, value);
    return length_squared > 1.0e-8
        ? value * rsqrt(length_squared)
        : fallback;
}

#endif
