Shader "Toy3d/Surface/Unlit"
{
    Version 2
    Usage Material
    Geometry Standard
    VertexFactories { Local, GPUSkin }

    Properties
    {
        base_color ("Base Color", Color) = (1.0, 1.0, 1.0, 1.0)
        base_color_texture ("Base Color Texture", Texture2D) = "white"
        material_sampler ("Material Sampler", Sampler) = LinearWrap
        alpha_cutoff ("Alpha Cutoff", Range(0, 1)) = 0.5
        uv_scale ("UV Scale", Float2) = (1.0, 1.0)
    }

    Variants
    {
        USE_VERTEX_COLOR : bool = false Stages { Vertex, Pixel }
        SURFACE_MODE : enum { Opaque, Masked } = Opaque Stages { Pixel }
    }

    HLSLINCLUDE
    float4 toy_unlit_base_color(ToySurfaceInput input)
    {
        float4 color = base_color * base_color_texture.Sample(material_sampler, input.uv * uv_scale);
#if TOY3D_VARIANT_USE_VERTEX_COLOR
        color *= input.color;
#endif
        return color;
    }
    float toy_unlit_coverage(ToySurfaceInput input)
    {
        return toy_unlit_base_color(input).a - alpha_cutoff;
    }
    ENDHLSL

    Pass "Forward"
    {
        Role Forward
        CoverageFunction toy_unlit_coverage
        Requires GraphicsBaseline
        PrimitiveTopology TriangleList
        Cull Back
        FrontFace CounterClockwise
        Fill Solid
        DepthTest GreaterEqual
        DepthWrite On
        Stencil Off
        Blend Off
        ColorWrite RGBA

        HLSLPS
        #pragma pixel ps_main

        float4 ps_main(ToySurfaceInput input)
        {
            return toy_unlit_base_color(input);
        }
        ENDHLSL
    }
}
