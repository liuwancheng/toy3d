Shader "Tests/InvalidState"
{
    Version 1
    Pass "Forward"
    {
        Cull Sideways
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        ENDHLSL
    }
}
