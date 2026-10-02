Shader "Project/Surface/Painted"
{
    Version 2
    Usage Material
    Geometry Custom
    VertexFactories { Local }
    Properties
    {
        base_color ("Base Color", Color) = (0.2, 0.55, 0.85, 1.0)
        stripe_scale ("Stripe Scale", Range(1.0, 20.0)) = 6.0
        stripe_strength ("Stripe Strength", Range(0.0, 1.0)) = 0.5
    }
    Pass "Forward"
    {
        Role Forward
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
        HLSLVS
        #pragma vertex vs_main
        #include "/Engine/ShaderIncludes/ToyCommon.hlsli"
        #include "/Project/ShaderIncludes/project_common.hlsli"
        struct VSInput { float4 position : POSITION0; float4 normal : NORMAL0; };
        struct VSOutput { float4 position : SV_Position; float3 world_position : TEXCOORD0; };
        VSOutput vs_main(VSInput input)
        {
            VSOutput output;
            const float4 world = mul(toy_object_to_world, float4(input.position.xyz, 1.0));
            output.position = mul(toy_view_projection, world);
            output.world_position = world.xyz;
            return output;
        }
        float4 ps_main(VSOutput input) : SV_Target0
        {
            const float stripe = toy_project_stripe(input.world_position.y * stripe_scale);
            return float4(base_color.rgb * lerp(1.0, stripe * 0.7 + 0.3, stripe_strength), base_color.a);
        }
        ENDHLSL

        HLSLPS
        #pragma pixel ps_main
        #include "/Engine/ShaderIncludes/ToyCommon.hlsli"
        #include "/Project/ShaderIncludes/project_common.hlsli"
        struct VSInput { float4 position : POSITION0; float4 normal : NORMAL0; };
        struct VSOutput { float4 position : SV_Position; float3 world_position : TEXCOORD0; };
        VSOutput vs_main(VSInput input)
        {
            VSOutput output;
            const float4 world = mul(toy_object_to_world, float4(input.position.xyz, 1.0));
            output.position = mul(toy_view_projection, world);
            output.world_position = world.xyz;
            return output;
        }
        float4 ps_main(VSOutput input) : SV_Target0
        {
            const float stripe = toy_project_stripe(input.world_position.y * stripe_scale);
            return float4(base_color.rgb * lerp(1.0, stripe * 0.7 + 0.3, stripe_strength), base_color.a);
        }
        ENDHLSL
    }
    Pass "ShadowDepth"
    {
        Role ShadowDepth
        Requires GraphicsBaseline
        PrimitiveTopology TriangleList
        Cull Back
        FrontFace CounterClockwise
        Fill Solid
        DepthTest GreaterEqual
        DepthWrite On
        Stencil Off
        Blend Off
        ColorWrite None

        HLSLVS
        #pragma vertex vs_main

        #include "/Engine/ShaderIncludes/ToyMeshVertex.hlsli"

        struct VSInput
        {
            float4 position : POSITION0;
            TOY3D_SKIN_VERTEX_INPUT
            float4 normal : NORMAL0;
        };

        float4 vs_main(VSInput input) : SV_Position
        {
            float3 mesh_position, mesh_normal;
            TOY3D_DEFORM_VERTEX(input, mesh_position, mesh_normal);
            const float4 world = mul(toy_object_to_world, float4(mesh_position, 1.0));
            float4 clip = mul(shadow_world_to_clip, world);
            const float3 normal = normalize(mul((float3x3)toy_object_normal_to_world, mesh_normal));
            const float NoL = saturate(abs(dot(normal, shadow_light_direction.xyz)));
            const float slope = min(sqrt(max(0.0, 1.0 - NoL * NoL)) / max(NoL, 1.0e-4),
                                    shadow_bias_parameters.z);
            const float bias = min(shadow_bias_parameters.x + shadow_bias_parameters.y * slope,
                                   shadow_bias_parameters.w);
            // Reversed-Z moves the caster away from the light, toward depth zero.
            clip.z = max(0.0, clip.z - bias * clip.w);
            clip.z = max(0.0, clip.z - stripe_strength * 0.0001 * clip.w);
            return clip;
        }

        ENDHLSL

        HLSLPS
        #pragma pixel ps_main

        void ps_main() {}
        ENDHLSL
    }
    Pass "HitProxy"
    {
        Role HitProxy
        Requires GraphicsBaseline
        PrimitiveTopology TriangleList
        Cull Off
        FrontFace CounterClockwise
        Fill Solid
        DepthTest GreaterEqual
        DepthWrite On
        Stencil Off
        Blend Off
        ColorWrite R

        HLSLVS
        #pragma vertex vs_main

        #include "/Engine/ShaderIncludes/ToyMeshVertex.hlsli"
        struct VSInput
        {
            float4 position : POSITION0;
            float4 normal : NORMAL0;
            TOY3D_SKIN_VERTEX_INPUT
        };
        float4 vs_main(VSInput input) : SV_Position
        {
            float3 mesh_position, mesh_normal;
            TOY3D_DEFORM_VERTEX(input, mesh_position, mesh_normal);
            return mul(toy_view_projection,
                       mul(toy_object_to_world, float4(mesh_position, 1.0)));
        }

        ENDHLSL

        HLSLPS
        #pragma pixel ps_main

        uint ps_main() : SV_Target0
        {
            // Each 16-bit lane is exactly representable by Float32 Parameters.
            return uint(hit_proxy_id_parts.x) | (uint(hit_proxy_id_parts.y) << 16u);
        }
        ENDHLSL
    }
}
