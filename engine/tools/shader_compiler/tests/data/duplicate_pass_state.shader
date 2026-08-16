Shader "Tests/DuplicatePassState"
{
    Version 1
    Pass "Forward"
    {
        Cull Back
        Cull Front
        HLSLPROGRAM
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0.0; }
        ENDHLSL
    }
}
