Shader "Toy3d/Test/TestPass"
{
    Version 2
    Usage Global

    Resources
    {
        Global
        {
            global_texture : Texture2D<Float4>
        }

        View
        {
            view_texture : Texture2D<Float4>
        }

        Material
        {
            source_texture : Texture2D<Float4>
            source_sampler : Sampler = PointWrap
        }
    }

    Pass "TestPass"
    {
        Role Global
        Requires GraphicsBaseline
        PrimitiveTopology TriangleList
        Cull Off
        FrontFace CounterClockwise
        Fill Solid
        DepthTest GreaterEqual
        DepthWrite On
        Stencil Off
        Blend Off
        ColorWrite RGBA

        HLSLVS
        #pragma vertex vs_main

        struct PixelInput
        {
            float4 position : SV_Position;
            float2 uv : TEXCOORD0;
        };

        PixelInput vs_main(uint vertex_id : SV_VertexID)
        {
            const float2 positions[3] = {
                float2(-1.0, -1.0),
                float2(3.0, -1.0),
                float2(-1.0, 3.0)};
            const float2 uvs[3] = {
                float2(0.0, 0.0),
                float2(2.0, 0.0),
                float2(0.0, 2.0)};
            PixelInput output;
            output.position = float4(positions[vertex_id], 1.0, 1.0);
            output.uv = uvs[vertex_id];
            return output;
        }

        float4 ps_main(PixelInput input) : SV_Target0
        {
            const float4 global_value = global_texture.Load(int3(0, 0, 0));
            const float4 view_value = view_texture.Load(int3(1, 0, 0));
            return source_texture.Sample(source_sampler, input.uv) *
                (global_value + view_value) * 0.5;
        }
        ENDHLSL

        HLSLPS
        #pragma pixel ps_main

        struct PixelInput
        {
            float4 position : SV_Position;
            float2 uv : TEXCOORD0;
        };

        PixelInput vs_main(uint vertex_id : SV_VertexID)
        {
            const float2 positions[3] = {
                float2(-1.0, -1.0),
                float2(3.0, -1.0),
                float2(-1.0, 3.0)};
            const float2 uvs[3] = {
                float2(0.0, 0.0),
                float2(2.0, 0.0),
                float2(0.0, 2.0)};
            PixelInput output;
            output.position = float4(positions[vertex_id], 1.0, 1.0);
            output.uv = uvs[vertex_id];
            return output;
        }

        float4 ps_main(PixelInput input) : SV_Target0
        {
            const float4 global_value = global_texture.Load(int3(0, 0, 0));
            const float4 view_value = view_texture.Load(int3(1, 0, 0));
            return source_texture.Sample(source_sampler, input.uv) *
                (global_value + view_value) * 0.5;
        }
        ENDHLSL
    }
}
