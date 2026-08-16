Shader "Tests/FrontendValid"
{
    Version 1

    Properties
    {
        tint ("Tint", Color) = (1.0, 0.5, 0.25, 1.0)
        roughness ("Roughness", Range(0.0, 1.0)) = 0.5
        source_texture ("Source", Texture2D) = "white"
    }

    Resources
    {
        Pass
        {
            scene_texture : Texture2D
            scene_sampler : Sampler = LinearClamp
        }

        Object
        {
            transforms : StructuredBuffer<float4x4>
        }
    }

    Variants
    {
        ENABLE_TINT : bool = true
        QUALITY : enum { Low, Medium, High } = Medium
    }

    HLSLINCLUDE
    float4 shared_color()
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    ENDHLSL

    Pass "Forward"
    {
        Requires GraphicsBaseline
        PrimitiveTopology TriangleList
        Cull Back
        FrontFace CounterClockwise
        Fill Solid
        DepthTest LessEqual
        DepthWrite On
        Stencil Off
        Blend Off
        ColorWrite RGBA

        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main

        float4 vs_main(float3 position : POSITION0) : SV_Position
        {
            return float4(position, 1.0);
        }

        float4 ps_main() : SV_Target0
        {
            return shared_color();
        }
        ENDHLSL
    }
}
