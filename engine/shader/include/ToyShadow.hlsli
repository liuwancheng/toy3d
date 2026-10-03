#ifndef TOY3D_SHADOW_INCLUDED
#define TOY3D_SHADOW_INCLUDED

#if defined(TOY3D_PASS_SHADOW_MODE) && TOY3D_PASS_SHADOW_MODE == TOY3D_PASS_SHADOW_MODE_PCF
// Gather returns raw depths in bottom-left, bottom-right, top-right, top-left order.
// Tile-local masks keep border taps fully lit and isolate neighboring cascades.
float4 toy_shadow_gather(float2 uv, int2 first_texel, int2 resolution,
                         float receiver_depth, float transition_scale)
{
    const float4 depths = shadow_atlas.Gather(shadow_sampler, uv);
    // UE-style depth transition, with the difference reversed for reversed-Z.
    // Slightly closer samples fade instead of producing binary self-shadow stripes.
    float4 lit = saturate((receiver_depth - depths) * transition_scale + 1.0);
    const int2 bottom_left = first_texel + int2(0, 1);
    const int2 bottom_right = first_texel + int2(1, 1);
    const int2 top_right = first_texel + int2(1, 0);
    if (any(bottom_left < 0) || any(bottom_left >= resolution)) lit.x = 1.0;
    if (any(bottom_right < 0) || any(bottom_right >= resolution)) lit.y = 1.0;
    if (any(top_right < 0) || any(top_right >= resolution)) lit.z = 1.0;
    if (any(first_texel < 0) || any(first_texel >= resolution)) lit.w = 1.0;
    return lit;
}

float toy_sample_shadow(float4x4 world_to_clip, float4 region,
                        float3 world_position, float transition_scale)
{
    const float4 clip = mul(world_to_clip, float4(world_position, 1.0));
    const float3 ndc = clip.xyz / clip.w;
    const float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;

    const float2 texel_position = uv * region.zw - 0.5;
    const float2 fraction = frac(texel_position);
    const int2 base_texel = int2(floor(texel_position));
    // Gather at the middle of each 2x2 footprint avoids rounding across
    // an integer texel boundary in the non-square atlas.
    const float2 center_uv = (region.xy + float2(base_texel) + 1.0) * shadow_texel_size.xy;
    const int2 resolution = int2(region.zw);
    // UV shifts express the same four 2x2 neighborhoods as Gather offsets,
    // without requiring Vulkan's optional shaderImageGatherExtended feature.
    const float4 top_left = toy_shadow_gather(
        center_uv + float2(-1.0, -1.0) * shadow_texel_size.xy,
        base_texel + int2(-1, -1), resolution, ndc.z, transition_scale);
    const float4 top_right = toy_shadow_gather(
        center_uv + float2(1.0, -1.0) * shadow_texel_size.xy,
        base_texel + int2(1, -1), resolution, ndc.z, transition_scale);
    const float4 bottom_left = toy_shadow_gather(
        center_uv + float2(-1.0, 1.0) * shadow_texel_size.xy,
        base_texel + int2(-1, 1), resolution, ndc.z, transition_scale);
    const float4 bottom_right = toy_shadow_gather(
        center_uv + float2(1.0, 1.0) * shadow_texel_size.xy,
        base_texel + int2(1, 1), resolution, ndc.z, transition_scale);

    // UE4.27's 3x3 linear PCF weights over a 4x4 raw-depth footprint.
    const float4 rows = float4(
        (1.0 - fraction.x) * top_left.w + top_left.z + top_right.w + fraction.x * top_right.z,
        (1.0 - fraction.x) * top_left.x + top_left.y + top_right.x + fraction.x * top_right.y,
        (1.0 - fraction.x) * bottom_left.w + bottom_left.z + bottom_right.w + fraction.x * bottom_right.z,
        (1.0 - fraction.x) * bottom_left.x + bottom_left.y + bottom_right.x + fraction.x * bottom_right.y);
    return saturate(dot(rows, float4(1.0 - fraction.y, 1.0, 1.0, fraction.y)) / 9.0);
}

float toy_sample_shadow_cascade(int cascade_index, float3 world_position, float receiver_scale)
{
    if (cascade_index == 0)
        return toy_sample_shadow(shadow_cascade_0_world_to_clip, shadow_cascade_0_region,
            world_position, shadow_receiver_parameters.x * receiver_scale);
    if (cascade_index == 1)
        return toy_sample_shadow(shadow_cascade_1_world_to_clip, shadow_cascade_1_region,
            world_position, shadow_receiver_parameters.y * receiver_scale);
    return toy_sample_shadow(shadow_cascade_2_world_to_clip, shadow_cascade_2_region,
        world_position, shadow_receiver_parameters.z * receiver_scale);
}

float toy_shadow_visibility(float3 world_position, float diffuse_term)
{
    float shadow_visibility = 1.0;
    if (shadow_distance_data.z > 0.5)
    {
        const float receiver_distance = mul(toy_view, float4(world_position, 1.0)).z;
        if (receiver_distance < shadow_distance_data.x)
        {
            // Grazing receivers need a wider transition. A zero Receiver Bias
            // keeps the cascade's base transition; one gives maximum attenuation.
            const float receiver_scale = lerp(1.0 - shadow_receiver_parameters.w, 1.0, diffuse_term);
            const int cascade_count = int(shadow_distance_data.z);
            int cascade_index = 0;
            float blend = 0.0;
            // Evaluate only active boundaries. Outside overlap, sample one
            // map; inside overlap, sample exactly the two neighboring maps.
            for (int boundary = 0; boundary < cascade_count - 1; ++boundary)
            {
                const float2 interval = boundary == 0 ? shadow_split_data.xy : shadow_split_data.zw;
                if (receiver_distance < interval.y)
                {
                    blend = saturate((receiver_distance - interval.x) / (interval.y - interval.x));
                    break;
                }
                cascade_index = boundary + 1;
            }
            float sampled = toy_sample_shadow_cascade(cascade_index, world_position, receiver_scale);
            if (blend > 0.0)
            {
                const float next_sample = toy_sample_shadow_cascade(cascade_index + 1,
                    world_position, receiver_scale);
                sampled = lerp(sampled, next_sample, blend);
            }
            const float fade = shadow_distance_data.y < shadow_distance_data.x
                ? saturate((receiver_distance - shadow_distance_data.y) /
                    (shadow_distance_data.x - shadow_distance_data.y)) : 0.0;
            shadow_visibility = lerp(sampled, 1.0, fade);
        }
    }

    return shadow_visibility;
}
#else
float toy_shadow_visibility(float3 world_position, float diffuse_term)
{
    return 1.0;
}
#endif
#endif
