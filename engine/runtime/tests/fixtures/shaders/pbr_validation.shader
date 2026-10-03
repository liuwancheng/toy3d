Shader "Toy3d/Test/PBRValidation"
{
    Version 2
    Usage Global
    Resources
    {
        Pass
        {
            validation_cube : TextureCube<Float4>
            validation_sampler : Sampler = TrilinearClamp
        }
    }
    Pass "PBRValidation"
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
        float4 vs_main(uint id : SV_VertexID) : SV_Position
        {
            return float4(id == 1 ? 3 : -1, id == 2 ? 3 : -1, 0, 1);
        }
        ENDHLSL
        HLSLPS
        #pragma pixel ps_main
        #include "/Engine/ShaderIncludes/ToyPBR.hlsli"
        float4 ps_main(float4 position : SV_Position) : SV_Target0
        {
            const uint x = (uint)position.x;
            const uint y = (uint)position.y;
            const float roughness = toy_mobile_roughness(x / 15.0);
            float value = 0;
            if (y == 0) value = toy_mobile_ggx(roughness, 1);
            else if (y == 1) value = toy_mobile_ggx(roughness, 0.5);
            else if (y == 2) value = toy_mobile_env_brdf(float3(0.04, 0.04, 0.04), 0.5, x / 15.0).r;
            else if (y == 3) value = toy_pbr_f0(float3(0.8, 0.2, 0.1), x / 15.0, 0.5).r;
            else if (y == 4) value = toy_pbr_point_attenuation(x * 100.0, 800.0);
            else if (y == 5)
            {
                const float3 directions[6] = {
                    float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0),
                    float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
                const float lod = x < 6 ? 0 : (x < 12 ? 0.5 : 2);
                value = validation_cube.SampleLevel(validation_sampler, directions[x % 6], lod).r;
            }
            else
            {
                const float nv = x / 15.0;
                const float3 v = float3(sqrt(max(0, 1 - nv * nv)), 0, nv);
                value = toy_pbr_direct(float3(0.5, 0.5, 0.5),
                    toy_mobile_env_brdf(float3(0.04, 0.04, 0.04), 0.5, nv),
                    0.5, float3(0, 0, 1), v, float3(0, 0, 1)).r;
            }
            // Exact IEEE bits survive RGBA8 readback; HDR and the GGX cap are not clipped.
            const uint bits = asuint(value);
            return float4(bits & 255, (bits >> 8) & 255, (bits >> 16) & 255, bits >> 24) / 255.0;
        }
        ENDHLSL
    }
}
