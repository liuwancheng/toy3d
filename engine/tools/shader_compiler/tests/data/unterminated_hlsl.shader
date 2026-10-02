Shader "Tests/UnterminatedHlsl"
{
    Version 2
    Usage Global
    Pass "Forward"
    {
        Role Global
        HLSLVS
        #pragma vertex vs_main
        #pragma pixel ps_main
