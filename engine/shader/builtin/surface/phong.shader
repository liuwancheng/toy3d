Shader "Toy3d/Surface/Phong"
{
    Version 2
    Usage Material
    Geometry Standard
    VertexFactories { Local, GPUSkin }

    Features { Lighting When Equal(USE_LIGHTING, true) Shadows When Equal(USE_LIGHTING, true) }

    Properties
    {
        base_color ("Base Color", Color) = (0.85, 0.32, 0.18, 1.0)
        ambient_color ("Ambient Color", Color) = (0.08, 0.10, 0.14, 1.0)
        specular_color ("Specular Color", Color) = (1.0, 0.92, 0.78, 1.0)
        specular_power ("Specular Power", Float) = 32.0
        specular_intensity ("Specular Intensity", Float) = 0.35
        surface_tint_texture ("Surface Tint Texture", Texture2D) = "white"
        material_sampler ("Material Sampler", Sampler) = TrilinearWrap
        alpha_cutoff ("Alpha Cutoff", Range(0, 1)) = 0.5
        uv_scale ("UV Scale", Float2) = (1.0, 1.0)
    }

    Variants
    {
        USE_VERTEX_COLOR : bool = false Stages { Vertex, Pixel }
        SURFACE_MODE : enum { Opaque, Masked } = Opaque Stages { Pixel }
        USE_LIGHTING : bool = true Stages { Pixel } Passes { Forward }
    }

    HLSLINCLUDE
    float4 toy_phong_base_color(ToySurfaceInput input)
    {
        float4 color = base_color * surface_tint_texture.Sample(material_sampler, input.uv * uv_scale);
#if TOY3D_VARIANT_USE_VERTEX_COLOR
        color *= input.color;
#endif
        return color;
    }
    float toy_phong_coverage(ToySurfaceInput input)
    {
        return toy_phong_base_color(input).a - alpha_cutoff;
    }
    ENDHLSL

    Pass "Forward"
    {
        Role Forward
        CoverageFunction toy_phong_coverage
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

        HLSLPS
        #pragma pixel ps_main

        #include "/Engine/ShaderIncludes/ToyLighting.hlsli"
        #include "/Engine/ShaderIncludes/ToyShadow.hlsli"

        float4 ps_main(ToySurfaceInput input)
        {
#if TOY3D_VARIANT_USE_LIGHTING
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
            const float4 surface_color = toy_phong_base_color(input);

            const float shadow_visibility = toy_shadow_visibility(input.world_position, diffuse_term);

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
                surface_color.rgb *
                    (ambient_color.rgb +
                     scene_light_color.rgb * (diffuse_term * shadow_visibility) + point_diffuse) +
                specular_color.rgb * scene_light_color.rgb *
                    (specular_term * specular_intensity * shadow_visibility);
            return float4(max(lit_color, 0.0), surface_color.a);
#else
            return toy_phong_base_color(input);
#endif
        }
        ENDHLSL
    }
}
