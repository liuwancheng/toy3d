Shader "Toy3d/Surface/PBR"
{
    Version 2
    Usage Material
    Geometry Standard
    VertexFactories { Local, GPUSkin }
    SurfaceInputs { Tangent }
    GeometryRequirements { TangentFrame When All(Equal(USE_LIGHTING, true), Equal(USE_NORMAL_MAP, true)) }
    Features
    {
        Lighting When Equal(USE_LIGHTING, true)
        Shadows When Equal(USE_LIGHTING, true)
        Environment When Equal(USE_LIGHTING, true)
    }
    Properties
    {
        base_color ("Base Color", Color) = (1, 1, 1, 1)
        base_color_texture ("Base Color Texture", Texture2D) = "white" Usage Color
        metallic ("Metallic", Range(0, 1)) = 0
        roughness ("Roughness", Range(0, 1)) = 0.5
        specular ("Specular", Range(0, 1)) = 0.5
        normal_texture ("Normal Texture", Texture2D) = "normal_flat" Usage Normal
        normal_scale ("Normal Scale", Range(0, 4)) = 1
        metallic_roughness_occlusion_texture ("MRO Texture (AO, Roughness, Metallic)", Texture2D) = "white_linear" Usage LinearData
        occlusion_strength ("Occlusion Strength", Range(0, 1)) = 1
        emissive_color ("Emissive Color", Color) = (0, 0, 0, 1)
        emissive_texture ("Emissive Texture", Texture2D) = "black" Usage Color
        uv_scale ("UV Scale", Float2) = (1, 1)
        alpha_cutoff ("Alpha Cutoff", Range(0, 1)) = 0.5
        material_sampler ("Material Sampler", Sampler) = TrilinearWrap
    }
    Variants
    {
        USE_LIGHTING : bool = true Stages { Pixel } Passes { Forward }
        USE_NORMAL_MAP : bool = false Stages { Pixel } Passes { Forward }
        USE_MRO_MAP : bool = false Stages { Pixel } Passes { Forward }
        USE_EMISSIVE_MAP : bool = false Stages { Pixel } Passes { Forward }
        USE_VERTEX_COLOR : bool = false Stages { Vertex, Pixel }
        SURFACE_MODE : enum { Opaque, Masked } = Opaque Stages { Pixel }
    }
    HLSLINCLUDE
    float4 toy_pbr_base_color(ToySurfaceInput input)
    {
        float4 color = base_color * base_color_texture.Sample(material_sampler, input.uv * uv_scale);
#if TOY3D_VARIANT_USE_VERTEX_COLOR
        color *= input.color;
#endif
        return color;
    }
    float toy_pbr_coverage(ToySurfaceInput input)
    {
        return toy_pbr_base_color(input).a - alpha_cutoff;
    }
    ENDHLSL
    Pass "Forward"
    {
        Role Forward
        CoverageFunction toy_pbr_coverage
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
        #include "/Engine/ShaderIncludes/ToyPBR.hlsli"
        #include "/Engine/ShaderIncludes/ToyShadow.hlsli"
        float4 ps_main(ToySurfaceInput input)
        {
            const float4 surface_color = toy_pbr_base_color(input);
            const float3 base = max(surface_color.rgb, 0.0);
            float3 emissive = max(emissive_color.rgb, 0.0);
#if TOY3D_VARIANT_USE_EMISSIVE_MAP
            emissive *= emissive_texture.Sample(material_sampler, input.uv * uv_scale).rgb;
#endif
#if TOY3D_VARIANT_USE_LIGHTING
            float3 normal = input.world_normal;
#if TOY3D_VARIANT_USE_NORMAL_MAP
            float3 tangent_normal = normal_texture.Sample(material_sampler, input.uv * uv_scale).xyz * 2.0 - 1.0;
            tangent_normal.xy *= normal_scale;
            tangent_normal = toy_safe_normalize(tangent_normal, float3(0, 0, 1));
            normal = toy_safe_normalize(input.world_tangent * tangent_normal.x +
                input.world_bitangent * tangent_normal.y + input.world_normal * tangent_normal.z, input.world_normal);
#endif
            float metal = metallic;
            float rough = roughness;
            float occlusion = 1.0;
#if TOY3D_VARIANT_USE_MRO_MAP
            const float3 mro = metallic_roughness_occlusion_texture.Sample(material_sampler, input.uv * uv_scale).rgb;
            metal *= mro.b;
            rough *= mro.g;
            occlusion = lerp(1.0, mro.r, occlusion_strength);
#endif
            const float r = toy_mobile_roughness(rough);
            const float3 view_direction = toy_safe_normalize(toy_camera_position - input.world_position, -toy_camera_direction);
            const float no_v = saturate(abs(dot(normal, view_direction)) + 1.0e-5);
            const float3 f0 = toy_pbr_f0(base, saturate(metal), specular);
            const float3 diffuse = base * (1.0 - saturate(metal));
            const float3 env_brdf = toy_mobile_env_brdf(f0, r, no_v);
            const float3 light_direction = toy_safe_normalize(scene_light_direction.xyz, float3(0, 0, -1));
            const float shadow = toy_shadow_visibility(input.world_position, saturate(dot(normal, light_direction)));
            float3 result = toy_pbr_direct(diffuse, env_brdf, r, normal, view_direction, light_direction) * scene_light_color.rgb * shadow;
            for (int i = 0; i < int(point_light_count); ++i)
            {
                const float3 position = float3(point_light_positions[0][i], point_light_positions[1][i], point_light_positions[2][i]);
                const float range = point_light_positions[3][i];
                const float3 radiance = float3(point_light_colors[0][i], point_light_colors[1][i], point_light_colors[2][i]);
                const float3 delta = position - input.world_position;
                const float attenuation = toy_pbr_point_attenuation(length(delta), range);
                result += toy_pbr_direct(diffuse, env_brdf, r, normal, view_direction,
                    toy_safe_normalize(delta, normal)) * radiance * attenuation;
            }
#if defined(TOY3D_PASS_ENVIRONMENT_MODE) && TOY3D_PASS_ENVIRONMENT_MODE == TOY3D_PASS_ENVIRONMENT_MODE_Sky
            const float3 reflected = reflect(-view_direction, normal);
            const float3 cube_direction = mul(environment_world_to_cube, float4(reflected, 0)).xyz;
            const float3 environment = environment_cube.SampleLevel(environment_sampler, cube_direction,
                r * environment_parameters.y).rgb * environment_parameters.x;
            result += environment * env_brdf * occlusion;
#endif
            return float4(max(result + emissive, 0.0), surface_color.a);
#else
            return float4(base + emissive, surface_color.a);
#endif
        }
        ENDHLSL
    }
}
