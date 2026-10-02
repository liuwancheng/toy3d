Shader "Toy3d/Test/TypedBuffer"
{
    Version 1
    Resources
    {
        Object
        {
            bone_matrix_buffer : Buffer<Float4>
        }
    }
    Pass "TypedBuffer"
    {
        Requires GraphicsBaseline
        PrimitiveTopology TriangleList
        Cull Off
        FrontFace CounterClockwise
        Fill Solid
        DepthTest Off
        DepthWrite Off
        Stencil Off
        Blend Off
        ColorWrite RGBA
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        struct PixelInput
        {
            float4 position : SV_Position;
            float4 color : COLOR0;
        };
        PixelInput vs_main(uint vertex_id : SV_VertexID)
        {
            PixelInput output;
            output.position = bone_matrix_buffer.Load(vertex_id);
            output.color = bone_matrix_buffer.Load(3);
            return output;
        }
        float4 ps_main(PixelInput input) : SV_Target0
        {
            return input.color;
        }
        ENDHLSL
    }
}
