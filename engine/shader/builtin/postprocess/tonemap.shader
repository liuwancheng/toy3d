Shader "Toy3d/PostProcess/Tonemap"
{
    Version 2
    Usage Global

    Parameters
    {
        Pass
        {
            exposure_ev : Float = 0.0
        }
    }

    Resources
    {
        Pass
        {
            scene_color : Texture2D<Float4>
            scene_sampler : Sampler = LinearClamp
        }
    }

    Pass "Tonemap"
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

        struct VSOutput
        {
            float4 clip_position : SV_Position;
            float2 uv : TEXCOORD0;
        };

        VSOutput vs_main(uint vertex_id : SV_VertexID)
        {
            VSOutput output;
            output.uv = float2((vertex_id << 1u) & 2u, vertex_id & 2u);
            output.clip_position = float4(
                output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0),
                0.0,
                1.0);
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

        float sanitize_hdr_component(float value)
        {
            return isfinite(value) ? clamp(value, 0.0, 65504.0) : 0.0;
        }

        float srgb_encode_component(float value)
        {
            const float clamped = saturate(value);
            return clamped <= 0.0031308
                ? 12.92 * clamped
                : 1.055 * pow(clamped, 1.0 / 2.4) - 0.055;
        }

        float4 ps_main(VSOutput input) : SV_Target0
        {
            const float3 sampled = scene_color.Sample(scene_sampler, input.uv).rgb;
            float3 exposed = sampled * exp2(clamp(exposure_ev, -32.0, 32.0));
            exposed = float3(
                sanitize_hdr_component(exposed.r),
                sanitize_hdr_component(exposed.g),
                sanitize_hdr_component(exposed.b));
            const float3 numerator = exposed * (2.51 * exposed + 0.03);
            const float3 denominator = exposed * (2.43 * exposed + 0.59) + 0.14;
            const float3 mapped = saturate(numerator / denominator);
            return float4(
                srgb_encode_component(mapped.r),
                srgb_encode_component(mapped.g),
                srgb_encode_component(mapped.b),
                1.0);
        }
        ENDHLSL
    }
}
