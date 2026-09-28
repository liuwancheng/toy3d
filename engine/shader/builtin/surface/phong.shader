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
    }

    Parameters
    {
        Pass
        {
            scene_light_direction : Float4 = (0.0, 0.0, -1.0, 0.0)
            scene_light_color : Float4 = (0.0, 0.0, 0.0, 0.0)
            point_light_positions : Float4x4
            point_light_colors : Float4x4
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
        };

        struct VSOutput
        {
            float4 clip_position : SV_Position;
            float3 world_position : TEXCOORD0;
            float3 world_normal : TEXCOORD1;
        };

        float3 toy_safe_normalize(float3 value, float3 fallback)
        {
            const float length_squared = dot(value, value);
            return length_squared > 1.0e-8
                ? value * rsqrt(length_squared)
                : fallback;
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
                (float3x3)toy_object_to_world,
                input.normal.xyz);
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
                surface_tint_texture.Load(int3(0, 0, 0)).rgb;

            float3 point_diffuse = float3(0, 0, 0);
            // Four column-packed lights use the existing matrix ABI; no descriptor arrays are required.
            for (int i = 0; i < 4; ++i)
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
                     scene_light_color.rgb * diffuse_term + point_diffuse) +
                specular_color.rgb * scene_light_color.rgb * (specular_term * specular_intensity);
            return float4(max(lit_color, 0.0), base_color.a);
        }
        ENDHLSL
    }
}
