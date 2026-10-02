Shader "Tests/DuplicatePassState"
{
    Version 2
    Usage Global
    Pass "Forward"
    {
        Role Global
        Cull Back
        Cull Front
        HLSLVS
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0.0; }
        ENDHLSL
    }
}
