Shader "Tests/InvalidState"
{
    Version 2
    Usage Global
    Pass "Forward"
    {
        Role Global
        Cull Sideways
        HLSLVS
        #pragma vertex vs_main
        ENDHLSL

        HLSLPS
        #pragma pixel ps_main
        ENDHLSL
    }
}
