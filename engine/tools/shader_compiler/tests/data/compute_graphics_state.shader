Shader "Tests/ComputeGraphicsState"
{
    Version 2
    Usage Global
    Pass "Compute"
    {
        Role Global
        DepthWrite Off
        HLSLCS
        #pragma compute cs_main
        [numthreads(1, 1, 1)] void cs_main() {}
        ENDHLSL
    }
}
