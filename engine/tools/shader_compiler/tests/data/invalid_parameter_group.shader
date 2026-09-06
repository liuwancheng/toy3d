Shader "Tests/InvalidParameterGroup"
{
    Version 1
    Parameters { View { exposure_ev : Float } }
    Pass "Main"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        ENDHLSL
    }
}
