Shader "Toy3d/Surface/Unlit"
{
    Version 1

    Properties
    {
        base_color ("Base Color", Color) = (1.0, 1.0, 1.0, 1.0)
        base_color_texture ("Base Color Texture", Texture2D) = "white"
        material_sampler ("Material Sampler", Sampler) = LinearWrap
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
            return float4(1.0, 1.0, 1.0, 1.0);
        }
        ENDHLSL
    }
}
