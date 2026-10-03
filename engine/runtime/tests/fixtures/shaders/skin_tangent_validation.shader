Shader "Toy3d/Test/SkinTangent"
{
    Version 2
    Usage Global
    Resources { Object { bone_matrix_buffer : Buffer<Float4> } }
    HLSLINCLUDE
    #include "/Engine/ShaderIncludes/ToySurface.hlsli"
    #include "/Engine/ShaderIncludes/ToyGPUSkin.hlsli"
    struct FrameOutput
    {
        float4 position : SV_Position;
        float3 normal : TEXCOORD0;
        float4 tangent : TEXCOORD1;
    };
    ENDHLSL
    Pass "SkinTangent"
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
        FrameOutput vs_main(uint id : SV_VertexID)
        {
            const uint4 indices = uint4(0, 1, 2, 3);
            const float4 weights = (1.0 / toy_num_bone_influences).xxxx;
            const float3 bind_normal = normalize(float3(1, 1, 0));
            const float4 bind_tangent = float4(normalize(float3(1, -1, 0)), 1);
            float3 position, normal, tangent, bitangent;
            toy_gpu_skin(bone_matrix_buffer, toy_num_bone_influences, indices, weights,
                uint4(4, 5, 6, 7), weights, float3(0, 0, 0), bind_normal, position, normal);
            toy_gpu_skin_frame(bone_matrix_buffer, toy_num_bone_influences, indices, weights,
                uint4(4, 5, 6, 7), weights, bind_normal, bind_tangent, tangent, bitangent);
            FrameOutput output;
            output.position = float4(id == 1 ? 3 : -1, id == 2 ? 3 : -1, 0, 1);
            output.normal = toy_surface_normalize(mul((float3x3)toy_object_normal_to_world, normal), float3(0, 0, 1));
            output.tangent = toy_surface_tangent_frame(output.normal,
                mul((float3x3)toy_object_to_world, tangent), mul((float3x3)toy_object_to_world, bitangent), bind_tangent.w);
            return output;
        }
        ENDHLSL
        HLSLPS
        #pragma pixel ps_main
        float4 ps_main(FrameOutput input) : SV_Target0
        {
            const uint row = (uint)input.position.y;
            const float3 bitangent = cross(input.normal, input.tangent.xyz) * input.tangent.w;
            float value = row == 0 ? input.normal.x : row == 1 ? input.normal.y :
                row == 2 ? input.tangent.x : row == 3 ? input.tangent.y : row == 4 ? bitangent.z : input.tangent.w;
            const uint bits = asuint(value);
            return float4(bits & 255, (bits >> 8) & 255, (bits >> 16) & 255, bits >> 24) / 255.0;
        }
        ENDHLSL
    }
}
