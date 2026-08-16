Shader "Tests/ReservedIdentifier"
{
    Version 1
    Properties
    {
        toy3d_internal ("Reserved", Float) = 0.0
    }
    Pass "Forward"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0.0; }
        ENDHLSL
    }
}
