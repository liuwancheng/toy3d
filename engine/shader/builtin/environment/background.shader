Shader "Toy3d/Environment/Background"
{
    Version 2
    Usage Global
    Parameters
    {
        Pass
        {
            inverse_projection : Float4x4
            view_to_cube : Float4x4
            environment_intensity : Float = 1.0
        }
    }
    Resources
    {
        Pass
        {
            background_cube : TextureCube<Float4>
            background_sampler : Sampler = LinearClamp
        }
    }
    Pass "EnvironmentBackground"
    {
        Role Global
        Requires GraphicsBaseline
        PrimitiveTopology TriangleList
        Cull Off
        FrontFace CounterClockwise
        Fill Solid
        DepthTest Equal
        DepthWrite Off
        Stencil Off
        Blend Off
        ColorWrite RGBA
        HLSLVS
        #pragma vertex vs_main
        struct VSOutput
        {
            float4 clip_position : SV_Position;
            float2 uv : TEXCOORD0;
        };
        VSOutput vs_main(uint vertex_id : SV_VertexID)
        {
            VSOutput output;
            output.uv = float2((vertex_id << 1u) & 2u, vertex_id & 2u);
            // Reversed-Z clear depth is zero: the background passes only untouched pixels.
            output.clip_position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
            return output;
        }
        ENDHLSL
        HLSLPS
        #pragma pixel ps_main
        struct VSOutput
        {
            float4 clip_position : SV_Position;
            float2 uv : TEXCOORD0;
        };
        float4 ps_main(VSOutput input) : SV_Target0
        {
            float2 ndc = input.uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
            float3 view_ray = mul(inverse_projection, float4(ndc, 1.0, 1.0)).xyz;
            float3 cube_ray = normalize(mul((float3x3)view_to_cube, view_ray));
            return float4(background_cube.SampleLevel(background_sampler, cube_ray, 0.0).rgb * environment_intensity, 1.0);
        }
        ENDHLSL
    }
}
