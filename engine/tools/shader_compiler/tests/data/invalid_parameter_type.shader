Shader "Tests/InvalidParameterType"
{
    Version 1
    Parameters { Pass { exposure_ev : Texture2D } }
    Pass "Main"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        ENDHLSL
    }
}
