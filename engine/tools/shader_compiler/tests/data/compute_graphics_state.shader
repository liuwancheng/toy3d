Shader "Tests/ComputeGraphicsState"
{
    Version 1
    Pass "Compute"
    {
        DepthWrite Off
        HLSLPROGRAM
        #pragma compute cs_main
        [numthreads(1, 1, 1)] void cs_main() {}
        ENDHLSL
    }
}
