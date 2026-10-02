Shader "Toy3d/Editor/HitProxy"
{
    Version 2
    Usage MeshPass
    Geometry Custom
    VertexFactories { Local, GPUSkin }

    Pass "HitProxy"
    {
        Role HitProxy
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

        HLSLVS
        #pragma vertex vs_main

        #include "/Engine/ShaderIncludes/ToyMeshVertex.hlsli"
        struct VSInput
        {
            float4 position : POSITION0;
            float4 normal : NORMAL0;
            TOY3D_SKIN_VERTEX_INPUT
        };
        float4 vs_main(VSInput input) : SV_Position
        {
            float3 mesh_position, mesh_normal;
            TOY3D_DEFORM_VERTEX(input, mesh_position, mesh_normal);
            return mul(toy_view_projection,
                       mul(toy_object_to_world, float4(mesh_position, 1.0)));
        }

        ENDHLSL

        HLSLPS
        #pragma pixel ps_main

        uint ps_main() : SV_Target0
        {
            // Each 16-bit lane is exactly representable by Float32 Parameters.
            return uint(hit_proxy_id_parts.x) | (uint(hit_proxy_id_parts.y) << 16u);
        }
        ENDHLSL
    }
}
