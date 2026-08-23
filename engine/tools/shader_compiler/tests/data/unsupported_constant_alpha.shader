Shader "Tests/UnsupportedConstantAlpha"
{
    Version 1
    Pass "Forward"
    {
        Blend
        {
            Color ConstantAlpha OneMinusConstantAlpha Add
            Alpha One Zero Add
        }
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        ENDHLSL
    }
}
