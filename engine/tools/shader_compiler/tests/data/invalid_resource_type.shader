Shader "Tests/InvalidResourceType"
{
    Version 1
    Resources
    {
        Material
        {
            bad_texture : Texture2D<Float4x4>
        }
    }
    Pass "Forward"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0.0; }
        ENDHLSL
    }
}
