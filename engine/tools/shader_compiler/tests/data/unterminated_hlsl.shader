Shader "Tests/UnterminatedHlsl"
{
    Version 1
    Pass "Forward"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
