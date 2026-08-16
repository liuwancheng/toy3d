Shader "Tests/DuplicateProperty"
{
    Version 1
    Properties
    {
        color ("Color", Color) = (1.0, 1.0, 1.0, 1.0)
        color ("Again", Color) = (0.0, 0.0, 0.0, 1.0)
    }
    Pass "Forward"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        ENDHLSL
    }
}
