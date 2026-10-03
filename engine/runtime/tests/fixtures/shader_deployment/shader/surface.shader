Shader "Project/Cook/Surface"
{
    Version 2
    Usage Material
    Geometry Standard
    VertexFactories { Local, GPUSkin }
    Properties
    {
        base_color ("Base Color", Color) = (1, 1, 1, 1)
    }
    Variants
    {
        EXTRA_COLOR : bool = false
        QUALITY : enum { Low, High } = Low
    }
    Pass "Forward"
    {
        Role Forward
        HLSLPS
        #pragma pixel shade
        float4 shade(ToySurfaceInput input)
        {
            return base_color * input.color;
        }
        ENDHLSL
    }
}
