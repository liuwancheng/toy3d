Shader "Project/Surface/Painted"
{
    Version 1
    Properties
    {
        base_color ("Base Color", Color) = (0.2, 0.55, 0.85, 1.0)
        stripe_scale ("Stripe Scale", Range(1.0, 20.0)) = 6.0
        stripe_strength ("Stripe Strength", Range(0.0, 1.0)) = 0.5
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
}
