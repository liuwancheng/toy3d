Shader "Tests/ParameterIdentifierConflict"
{
    Version 2
    Usage Global
    Parameters { Pass { scene_color : Float4 } }
    Resources { Pass { scene_color : Texture2D<Float4> } }
    Pass "Main"
    {
        Role Global
        HLSLVS
        #pragma vertex vs_main
        ENDHLSL
    }
}
