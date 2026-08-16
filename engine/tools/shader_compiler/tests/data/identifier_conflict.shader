Shader "Tests/IdentifierConflict"
{
    Version 1
    Properties
    {
        shared_name ("Shared", Float) = 1.0
    }
    Resources
    {
        Material
        {
            shared_name : Sampler
        }
    }
    Pass "Forward"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0.0; }
        ENDHLSL
    }
}
