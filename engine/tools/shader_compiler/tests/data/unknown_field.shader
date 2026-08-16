Shader "Tests/UnknownField"
{
    Version 1
    Fallback "Hidden/InternalError"
    Pass "Forward"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        ENDHLSL
    }
}
