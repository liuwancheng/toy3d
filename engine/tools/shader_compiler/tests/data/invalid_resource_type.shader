Shader "Tests/InvalidResourceType"
{
    Version 2
    Usage Global
    Resources
    {
        Material
        {
            bad_texture : Texture2D<Float4x4>
        }
    }
    Pass "Forward"
    {
        Role Global
        HLSLVS
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0.0; }
        ENDHLSL
    }
}
