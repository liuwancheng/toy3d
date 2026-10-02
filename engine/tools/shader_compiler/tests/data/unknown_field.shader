Shader "Tests/UnknownField"
{
    Version 2
    Usage Global
    Fallback "Hidden/InternalError"
    Pass "Forward"
    {
        Role Global
        HLSLVS
        #pragma vertex vs_main
        ENDHLSL

        HLSLPS
        #pragma pixel ps_main
        ENDHLSL
    }
}
