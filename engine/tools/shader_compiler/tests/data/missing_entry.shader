Shader "Tests/MissingEntry"
{
    Version 2
    Usage Global
    Pass "Forward"
    {
        Role Global
        HLSLPS
        #pragma pixel ps_main
        ENDHLSL
    }
}
