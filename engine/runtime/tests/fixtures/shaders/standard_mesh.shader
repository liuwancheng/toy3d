Shader "Toy3d/Test/StandardMesh"
{
    Version 2
    Usage Material
    Geometry Standard
    VertexFactories { Local, GPUSkin }
    Features { Lighting Shadows }
    Properties
    {
        coverage ("Coverage", Float) = 1.0
    }
    Variants
    {
        SURFACE_MODE : enum { Opaque, Masked } = Masked
    }
    HLSLINCLUDE
    float toy_test_coverage(ToySurfaceInput input)
    {
        return coverage - 0.5;
    }
    ENDHLSL
    Pass "Forward"
    {
        Role Forward
        CoverageFunction toy_test_coverage
        HLSLPS
        #pragma pixel shade
        float4 shade(ToySurfaceInput input)
        {
            return float4(1, 0, 0, 1);
        }
        ENDHLSL
    }
}
