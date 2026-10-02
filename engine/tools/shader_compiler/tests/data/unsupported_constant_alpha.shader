Shader "Tests/UnsupportedConstantAlpha"
{
    Version 2
    Usage Global
    Pass "Forward"
    {
        Role Global
        Blend
        {
            Color ConstantAlpha OneMinusConstantAlpha Add
            Alpha One Zero Add
        }
        HLSLVS
        #pragma vertex vs_main
        ENDHLSL

        HLSLPS
        #pragma pixel ps_main
        ENDHLSL
    }
}
