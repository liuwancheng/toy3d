Shader "Tests//Invalid"
{
    Version 2
    Usage Global
    Pass "Forward"
    {
        Role Global
        HLSLVS
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0.0; }
        ENDHLSL
    }
}
