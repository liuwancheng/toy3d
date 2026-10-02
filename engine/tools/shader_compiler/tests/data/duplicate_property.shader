Shader "Tests/DuplicateProperty"
{
    Version 2
    Usage Global
    Properties
    {
        color ("Color", Color) = (1.0, 1.0, 1.0, 1.0)
        color ("Again", Color) = (0.0, 0.0, 0.0, 1.0)
    }
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
