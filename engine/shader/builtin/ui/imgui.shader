Shader "Toy3d/UI/ImGui"
{
    Version 1

    Parameters
    {
        Pass
        {
            projection : Float4x4
        }
    }

    Resources
    {
        Pass
        {
            font_texture : Texture2D<Float4>
            font_sampler : Sampler = LinearClamp
        }
    }

    Pass "ImGui"
    {
        Requires GraphicsBaseline
        PrimitiveTopology TriangleList
        Cull Off
        FrontFace CounterClockwise
        Fill Solid
        DepthTest Off
        DepthWrite Off
        Stencil Off
        Blend
        {
            Color SrcAlpha OneMinusSrcAlpha Add
            Alpha One OneMinusSrcAlpha Add
        }
        ColorWrite RGBA

        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main

        struct VSInput
        {
            float2 position : POSITION0;
            float2 uv : TEXCOORD0;
            float4 color : COLOR0;
        };

        struct VSOutput
        {
            float4 clip_position : SV_Position;
            float2 uv : TEXCOORD0;
            float4 color : COLOR0;
        };

        VSOutput vs_main(VSInput input)
        {
            VSOutput output;
            output.clip_position = mul(projection, float4(input.position, 0.0, 1.0));
            output.uv = input.uv;
            output.color = input.color;
            return output;
        }

        float4 ps_main(VSOutput input) : SV_Target0
        {
            return input.color * font_texture.Sample(font_sampler, input.uv);
        }
        ENDHLSL
    }
}
