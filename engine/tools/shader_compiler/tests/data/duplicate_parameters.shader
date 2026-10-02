Shader "Tests/DuplicateParameters"
{
    Version 2
    Usage Global
    Parameters { Pass { exposure_ev : Float } }
    Parameters { Pass { projection : Float4x4 } }
    Pass "Main"
    {
        Role Global
        HLSLVS
        #pragma vertex vs_main
        ENDHLSL
    }
}
