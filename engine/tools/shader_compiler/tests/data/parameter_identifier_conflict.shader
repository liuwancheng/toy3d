Shader "Tests/ParameterIdentifierConflict"
{
    Version 1
    Parameters { Pass { scene_color : Float4 } }
    Resources { Pass { scene_color : Texture2D<Float4> } }
    Pass "Main"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        ENDHLSL
    }
}
