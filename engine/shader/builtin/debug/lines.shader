Shader "Toy3d/Debug/Lines"
{
    Version 2
    Usage Global
    Parameters
    {
        Pass
        {
            view_projection : Float4x4
        }
    }
    Pass "DebugLines"
    {
        Role Global
        Requires GraphicsBaseline
        PrimitiveTopology LineList
        Cull Off
        FrontFace CounterClockwise
        Fill Solid
        DepthTest GreaterEqual
        DepthWrite Off
        Stencil Off
        Blend Off
        ColorWrite RGBA
        HLSLVS
        #pragma vertex vs_main
        struct VSInput { float4 position : POSITION0; float4 color : COLOR0; };
        struct VSOutput { float4 position : SV_Position; float4 color : COLOR0; };
        VSOutput vs_main(VSInput input)
        {
            VSOutput output;
            output.position = mul(view_projection, float4(input.position.xyz, 1.0));
            output.color = input.color;
            return output;
        }
        ENDHLSL
        HLSLPS
        #pragma pixel ps_main
        struct VSOutput { float4 position : SV_Position; float4 color : COLOR0; };
        float4 ps_main(VSOutput input) : SV_Target0 { return input.color; }
        ENDHLSL
    }
}
