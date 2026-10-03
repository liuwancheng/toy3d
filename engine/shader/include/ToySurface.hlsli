#ifndef TOY3D_SURFACE_INCLUDED
#define TOY3D_SURFACE_INCLUDED

// This interface is shared by compiler-owned mesh entry points. User functions
// receive data, rather than owning rasterizer outputs or engine bindings.
struct ToySurfaceVaryings
{
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 world_tangent : TEXCOORD3;
    float4 color : TEXCOORD4;
};

struct ToySurfaceInput
{
    float3 world_position;
    float3 world_normal;
    float3 world_tangent;
    float3 world_bitangent;
    float2 uv;
    float4 color;
    bool front_face;
};

float3 toy_surface_normalize(float3 value, float3 fallback)
{
    const float squared = dot(value, value);
    return squared > 1.0e-8 ? value * rsqrt(squared) : fallback;
}

// Affine T/B use the position transform; N arrives through inverse transpose.
// Recover orientation after all skin/object transforms, including negative scale.
float4 toy_surface_tangent_frame(float3 normal, float3 tangent, float3 bitangent, float fallback_sign)
{
    const float3 axis = abs(normal.z) < 0.9 ? float3(0, 0, 1) : float3(0, 1, 0);
    const float3 orthogonal = toy_surface_normalize(tangent - normal * dot(tangent, normal), normalize(cross(axis, normal)));
    const float orientation = dot(cross(normal, orthogonal), bitangent);
    const float sign = abs(orientation) > 1.0e-8 ? (orientation < 0 ? -1 : 1) : fallback_sign;
    return float4(orthogonal, sign);
}

ToySurfaceInput toy_surface_input(ToySurfaceVaryings input, bool front_face)
{
    ToySurfaceInput result;
    result.world_position = input.world_position;
    result.world_normal = toy_surface_normalize(input.world_normal, float3(0, 0, 1));
    float3 tangent = input.world_tangent.xyz - result.world_normal * dot(input.world_tangent.xyz, result.world_normal);
    const float3 axis = abs(result.world_normal.z) < 0.9 ? float3(0, 0, 1) : float3(0, 1, 0);
    result.world_tangent = toy_surface_normalize(tangent, normalize(cross(axis, result.world_normal)));
    result.world_bitangent = cross(result.world_normal, result.world_tangent) * input.world_tangent.w;
    if (!front_face)
    {
        result.world_normal = -result.world_normal;
        result.world_bitangent = -result.world_bitangent;
    }
    result.uv = input.uv;
    result.color = input.color;
    result.front_face = front_face;
    return result;
}

#endif
