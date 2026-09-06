Shader "Tests/DuplicateParameters"
{
    Version 1
    Parameters { Pass { exposure_ev : Float } }
    Parameters { Pass { projection : Float4x4 } }
    Pass "Main"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        ENDHLSL
    }
}
