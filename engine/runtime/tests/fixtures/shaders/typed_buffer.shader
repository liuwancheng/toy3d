Shader "Toy3d/Test/TypedBuffer"
{
    Version 2
    Usage Global
    Resources
    {
        Object
        {
            bone_matrix_buffer : Buffer<Float4>
        }
    }
    Pass "TypedBuffer"
    {
        Role Global
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
        HLSLVS
        #pragma vertex vs_main
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

        HLSLPS
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
