Shader "Tests/InvalidParameterType"
{
    Version 2
    Usage Global
    Parameters { Pass { exposure_ev : Texture2D } }
    Pass "Main"
    {
        Role Global
        HLSLVS
        #pragma vertex vs_main
        ENDHLSL
    }
}
