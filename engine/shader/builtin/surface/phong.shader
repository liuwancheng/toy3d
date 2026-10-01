Shader "Toy3d/Surface/Phong"
{
    Version 1

    Properties
    {
        base_color ("Base Color", Color) = (0.85, 0.32, 0.18, 1.0)
        ambient_color ("Ambient Color", Color) = (0.08, 0.10, 0.14, 1.0)
        specular_color ("Specular Color", Color) = (1.0, 0.92, 0.78, 1.0)
        specular_power ("Specular Power", Float) = 32.0
        specular_intensity ("Specular Intensity", Float) = 0.35
        surface_tint_texture ("Surface Tint Texture", Texture2D) = "white"
        material_sampler ("Material Sampler", Sampler) = TrilinearWrap
        uv_scale ("UV Scale", Float2) = (1.0, 1.0)
    }

    Parameters
    {
        Pass
        {
            scene_light_direction : Float4 = (0.0, 0.0, -1.0, 0.0)
            scene_light_color : Float4 = (0.0, 0.0, 0.0, 0.0)
            point_light_positions : Float4x4
            point_light_colors : Float4x4
            point_light_count : Float
            shadow_cascade_0_world_to_clip : Float4x4
            shadow_cascade_1_world_to_clip : Float4x4
            shadow_cascade_2_world_to_clip : Float4x4
            shadow_distance_data : Float4
            shadow_split_data : Float4
            shadow_cascade_0_region : Float4
            shadow_cascade_1_region : Float4
            shadow_cascade_2_region : Float4
            shadow_texel_size : Float4
            shadow_receiver_parameters : Float4
        }
    }

    Resources
    {
        Pass
        {
            shadow_atlas : Texture2D<Float>
            shadow_sampler : Sampler
        }
    }

    Pass "Forward"
    {
        Requires GraphicsBaseline
        PrimitiveTopology TriangleList
        Cull Back
        FrontFace CounterClockwise
        Fill Solid
        DepthTest GreaterEqual
        DepthWrite On
        Stencil Off
        Blend Off
        ColorWrite RGBA

        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main

        struct VSInput
        {
            float4 position : POSITION0;
            float4 normal : NORMAL0;
            float2 uv : TEXCOORD0;
        };

        struct VSOutput
        {
            float4 clip_position : SV_Position;
            float3 world_position : TEXCOORD0;
            float3 world_normal : TEXCOORD1;
            float2 uv : TEXCOORD2;
        };

        float3 toy_safe_normalize(float3 value, float3 fallback)
        {
            const float length_squared = dot(value, value);
            return length_squared > 1.0e-8
                ? value * rsqrt(length_squared)
                : fallback;
        }

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

        VSOutput vs_main(VSInput input)
        {
            VSOutput output;
            const float4 world_position =
                mul(toy_object_to_world, float4(input.position.xyz, 1.0));
            output.clip_position =
                mul(toy_view_projection, world_position);
            output.world_position = world_position.xyz;
            output.world_normal = mul(
                (float3x3)toy_object_normal_to_world,
                input.normal.xyz);
            output.uv = input.uv;
            return output;
        }

        float4 ps_main(VSOutput input) : SV_Target0
        {
            const float3 normal = toy_safe_normalize(
                input.world_normal,
                float3(0.0, 0.0, 1.0));
            const float3 light_direction = toy_safe_normalize(
                scene_light_direction.xyz,
                float3(0.0, 0.0, -1.0));
            const float3 view_direction = toy_safe_normalize(
                toy_camera_position - input.world_position,
                -toy_camera_direction);
            const float diffuse_term = saturate(dot(normal, light_direction));
            const float3 reflected_light = reflect(-light_direction, normal);
            const float specular_term = pow(
                saturate(dot(view_direction, reflected_light)),
                max(specular_power, 1.0));
            const float3 texture_tint =
                surface_tint_texture.Sample(material_sampler, input.uv * uv_scale).rgb;

            float shadow_visibility = 1.0;
            if (toy_receives_shadows > 0.5 && shadow_distance_data.z > 0.5)
            {
                const float receiver_distance = mul(toy_view, float4(input.world_position, 1.0)).z;
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
                    float sampled = toy_sample_shadow_cascade(cascade_index, input.world_position, receiver_scale);
                    if (blend > 0.0)
                    {
                        const float next_sample = toy_sample_shadow_cascade(cascade_index + 1,
                            input.world_position, receiver_scale);
                        sampled = lerp(sampled, next_sample, blend);
                    }
                    const float fade = shadow_distance_data.y < shadow_distance_data.x
                        ? saturate((receiver_distance - shadow_distance_data.y) /
                            (shadow_distance_data.x - shadow_distance_data.y)) : 0.0;
                    shadow_visibility = lerp(sampled, 1.0, fade);
                }
            }

            float3 point_diffuse = float3(0, 0, 0);
            // The pass supplies the number of valid columns in the fixed-capacity light matrices.
            for (int i = 0; i < int(point_light_count); ++i)
            {
                const float3 position = float3(point_light_positions[0][i], point_light_positions[1][i], point_light_positions[2][i]);
                const float radius = point_light_positions[3][i];
                const float3 radiance = float3(point_light_colors[0][i], point_light_colors[1][i], point_light_colors[2][i]);
                const float3 delta = position - input.world_position;
                const float distance = length(delta);
                const float attenuation = radius > 0.0 ? saturate(1.0 - distance / radius) : 0.0;
                point_diffuse += radiance * attenuation * attenuation *
                    saturate(dot(normal, toy_safe_normalize(delta, float3(0, 1, 0))));
            }

            const float3 lit_color =
                base_color.rgb * texture_tint *
                    (ambient_color.rgb +
                     scene_light_color.rgb * (diffuse_term * shadow_visibility) + point_diffuse) +
                specular_color.rgb * scene_light_color.rgb *
                    (specular_term * specular_intensity * shadow_visibility);
            return float4(max(lit_color, 0.0), base_color.a);
        }
        ENDHLSL
    }
}
