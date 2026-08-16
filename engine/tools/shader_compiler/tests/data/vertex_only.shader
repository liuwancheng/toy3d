Shader "Tests/VertexOnly"
{
    Version 1
    Pass "DepthOnly"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0.0; }
        ENDHLSL
    }
}
