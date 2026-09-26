Shader "Toy3d/Editor/HitProxy"
{
    Version 1

    Parameters
    {
        Pass
        {
            hit_proxy_id_parts : Float2 = (0.0, 0.0)
        }
    }

    Pass "HitProxy"
    {
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

        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main

        float4 vs_main(float4 position : POSITION0) : SV_Position
        {
            return mul(toy_view_projection,
                       mul(toy_object_to_world, float4(position.xyz, 1.0)));
        }

        uint ps_main() : SV_Target0
        {
            // Each 16-bit lane is exactly representable by Float32 Parameters v1.
            return uint(hit_proxy_id_parts.x) | (uint(hit_proxy_id_parts.y) << 16u);
        }
        ENDHLSL
    }
}
