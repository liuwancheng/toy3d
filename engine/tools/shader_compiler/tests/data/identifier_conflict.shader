Shader "Tests/IdentifierConflict"
{
    Version 2
    Usage Global
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
        Role Global
        HLSLVS
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0.0; }
        ENDHLSL
    }
}
