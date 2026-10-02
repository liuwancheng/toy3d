Shader "Toy3d/Test/GPUSkin"
{
    Version 1
    Resources
    {
        Object
        {
            bone_matrix_buffer : Buffer<Float4>
        }
    }
    Pass "GPUSkin"
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
        #include "/Engine/ShaderIncludes/ToyGPUSkin.hlsli"
        struct VertexInput
        {
            float4 position : POSITION0;
            float4 normal : NORMAL0;
            uint4 bone_indices : BLENDINDICES0;
            float4 bone_weights : BLENDWEIGHT0;
            uint4 extra_bone_indices : BLENDINDICES1;
            float4 extra_bone_weights : BLENDWEIGHT1;
        };
        struct PixelInput
        {
            float4 position : SV_Position;
            float3 normal : COLOR0;
        };
        PixelInput vs_main(VertexInput input)
        {
            PixelInput output;
            float3 position;
            toy_gpu_skin(bone_matrix_buffer, toy_num_bone_influences,
                         input.bone_indices, input.bone_weights,
                         input.extra_bone_indices, input.extra_bone_weights,
                         input.position.xyz, input.normal.xyz, position, output.normal);
            output.position = float4(position, 1);
            return output;
        }
        float4 ps_main(PixelInput input) : SV_Target0
        {
            return float4(input.normal, 1);
        }
        ENDHLSL
    }
}
