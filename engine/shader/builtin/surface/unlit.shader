Shader "Toy3d/Surface/Unlit"
{
    Version 1

    Properties
    {
        base_color ("Base Color", Color) = (1.0, 1.0, 1.0, 1.0)
        base_color_texture ("Base Color Texture", Texture2D) = "white"
        material_sampler ("Material Sampler", Sampler) = LinearWrap
        uv_scale ("UV Scale", Float2) = (1.0, 1.0)
    }

    Variants
    {
        USE_VERTEX_COLOR : bool = false
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
            float2 uv : TEXCOORD0;
        };

        struct VSOutput
        {
            float4 clip_position : SV_Position;
            float2 uv : TEXCOORD0;
        };

        VSOutput vs_main(VSInput input)
        {
            VSOutput output;
            const float4 world_position =
                mul(toy_object_to_world, float4(input.position.xyz, 1.0));
            output.clip_position = mul(toy_view_projection, world_position);
            output.uv = input.uv;
            return output;
        }

        float4 ps_main(VSOutput input) : SV_Target0
        {
            return base_color * base_color_texture.Sample(material_sampler, input.uv * uv_scale);
        }
        ENDHLSL
    }
}
