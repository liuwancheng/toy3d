Shader "Tests/InvalidParameterGroup"
{
    Version 2
    Usage Global
    Parameters { View { exposure_ev : Float } }
    Pass "Main"
    {
        Role Global
        HLSLVS
        #pragma vertex vs_main
        ENDHLSL
    }
}
