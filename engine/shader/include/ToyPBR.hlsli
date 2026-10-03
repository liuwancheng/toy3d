#ifndef TOY3D_PBR_INCLUDED
#define TOY3D_PBR_INCLUDED

#include "/Engine/ShaderIncludes/ToyBRDF.hlsli"
#include "/Engine/ShaderIncludes/ToyLighting.hlsli"

float3 toy_pbr_f0(float3 base_color, float metallic, float specular)
{
    return lerp((0.08 * specular).xxx, base_color, metallic);
}

float3 toy_pbr_direct(float3 diffuse, float3 env_brdf, float roughness,
                      float3 normal, float3 view_direction, float3 light_direction)
{
    const float no_l = saturate(dot(normal, light_direction));
    const float3 halfway = toy_safe_normalize(view_direction + light_direction, normal);
    const float no_h = saturate(dot(normal, halfway));
    const float specular = toy_mobile_ggx(roughness, no_h) * (0.25 * roughness + 0.25);
    return (diffuse / toy_pi + env_brdf * specular) * no_l;
}

// Toy3d's bounded point-light policy: scene units are cm, distance term uses m.
float toy_pbr_point_attenuation(float distance_cm, float range_cm)
{
    if (range_cm <= 0.0)
    {
        return 0.0;
    }
    const float distance_m = distance_cm * 0.01;
    const float ratio = distance_cm / range_cm;
    const float squared_ratio = ratio * ratio;
    const float cutoff = saturate(1.0 - squared_ratio * squared_ratio);
    return cutoff * cutoff / max(distance_m * distance_m, 0.0001);
}

#endif
