Shader "Toy3d/ShadowDepth/Default"
{
    Version 1

    Parameters
    {
        Pass
        {
            shadow_world_to_clip : Float4x4
            shadow_light_direction : Float4
            shadow_bias_parameters : Float4
        }
    }

    Pass "ShadowDepth"
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
        ColorWrite None

        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main

        struct VSInput
        {
            float4 position : POSITION0;
            float4 normal : NORMAL0;
        };

        float4 vs_main(VSInput input) : SV_Position
        {
            const float4 world = mul(toy_object_to_world, float4(input.position.xyz, 1.0));
            float4 clip = mul(shadow_world_to_clip, world);
            const float3 normal = normalize(mul((float3x3)toy_object_normal_to_world, input.normal.xyz));
            const float NoL = saturate(abs(dot(normal, shadow_light_direction.xyz)));
            const float slope = min(sqrt(max(0.0, 1.0 - NoL * NoL)) / max(NoL, 1.0e-4),
                                    shadow_bias_parameters.z);
            const float bias = min(shadow_bias_parameters.x + shadow_bias_parameters.y * slope,
                                   shadow_bias_parameters.w);
            // Reversed-Z moves the caster away from the light, toward depth zero.
            clip.z = max(0.0, clip.z - bias * clip.w);
            return clip;
        }

        void ps_main() {}
        ENDHLSL
    }
}
