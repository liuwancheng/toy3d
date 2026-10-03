#include "drivers/rhi/rhi_descriptors.h"

#include <iostream>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::RHITextureDesc make_texture_desc(toy3d::PixelFormat format, toy3d::RHIResourceUsage usage)
    {
        toy3d::RHITextureDesc desc;
        desc.width = 64;
        desc.height = 64;
        desc.format = format;
        desc.usage = usage;
        return desc;
    }

    toy3d::RHITextureViewDesc make_view_desc(toy3d::RHIResourceViewType type, toy3d::PixelFormat format,
                                             toy3d::RHITextureAspect aspect)
    {
        toy3d::RHITextureViewDesc desc;
        desc.type = type;
        desc.format = format;
        desc.subresources.aspect = aspect;
        return desc;
    }
} // namespace

int main()
{
    using namespace toy3d;

    const RHITextureDesc scene_color = make_texture_desc(
        PixelFormat::R16G16B16A16Float, RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource);
    RHIFormatCapabilities scene_color_support;
    scene_color_support.usage = RHIFormatUsage::RenderTarget | RHIFormatUsage::Sampled;
    check(static_cast<bool>(validate_texture_format_capabilities(scene_color, scene_color_support)),
          "RGBA16F must validate when render-target and sampled usage are both supported");

    RHIFormatCapabilities missing_sampled_support;
    missing_sampled_support.usage = RHIFormatUsage::RenderTarget;
    const RHIStatus missing_sampled_status = validate_texture_format_capabilities(scene_color, missing_sampled_support);
    check(!missing_sampled_status && missing_sampled_status.code() == RHIErrorCode::Unsupported &&
              !missing_sampled_status.message().empty(),
          "RGBA16F must fail diagnostically when sampled usage is unavailable");

    const RHITextureViewDesc color_target_view =
        make_view_desc(RHIResourceViewType::RenderTarget, PixelFormat::R16G16B16A16Float, RHITextureAspect::Color);
    const RHITextureViewDesc color_sampled_view =
        make_view_desc(RHIResourceViewType::ShaderResource, PixelFormat::R16G16B16A16Float, RHITextureAspect::Color);
    check(static_cast<bool>(validate_texture_view_desc(scene_color, color_target_view)),
          "RGBA16F render-target view contract must validate");
    check(static_cast<bool>(validate_texture_view_desc(scene_color, color_sampled_view)),
          "RGBA16F sampled view contract must validate");

    const RHITextureDesc scene_depth = make_texture_desc(
        PixelFormat::D24UNormS8UInt, RHIResourceUsage::DepthStencil | RHIResourceUsage::ShaderResource);
    RHIFormatCapabilities scene_depth_support;
    scene_depth_support.usage = RHIFormatUsage::DepthStencil | RHIFormatUsage::Sampled;
    check(static_cast<bool>(validate_texture_format_capabilities(scene_depth, scene_depth_support)),
          "D24S8 must validate when depth-stencil and sampled usage are both supported");

    const RHITextureViewDesc depth_stencil_view =
        make_view_desc(RHIResourceViewType::DepthStencil, PixelFormat::D24UNormS8UInt, RHITextureAspect::DepthStencil);
    const RHITextureViewDesc depth_sampled_view =
        make_view_desc(RHIResourceViewType::ShaderResource, PixelFormat::D24UNormS8UInt, RHITextureAspect::Depth);
    check(static_cast<bool>(validate_texture_view_desc(scene_depth, depth_stencil_view)),
          "D24S8 attachment view must select depth and stencil aspects");
    check(static_cast<bool>(validate_texture_view_desc(scene_depth, depth_sampled_view)),
          "D24S8 sampled view must expose the depth aspect only");

    RHITextureViewDesc invalid_depth_sampled_view = depth_sampled_view;
    invalid_depth_sampled_view.subresources.aspect = RHITextureAspect::DepthStencil;
    const RHIStatus invalid_depth_sampled_status = validate_texture_view_desc(scene_depth, invalid_depth_sampled_view);
    check(!invalid_depth_sampled_status && invalid_depth_sampled_status.code() == RHIErrorCode::InvalidArgument &&
              !invalid_depth_sampled_status.message().empty(),
          "D24S8 sampled views must reject a combined depth-stencil aspect diagnostically");

    RHITextureViewDesc incompatible_format_view = depth_sampled_view;
    incompatible_format_view.format = PixelFormat::D32Float;
    const RHIStatus incompatible_format_status = validate_texture_view_desc(scene_depth, incompatible_format_view);
    check(!incompatible_format_status && incompatible_format_status.code() == RHIErrorCode::Unsupported &&
              !incompatible_format_status.message().empty(),
          "public texture views must reject format reinterpretation diagnostically");

    RHITextureDesc cube = scene_color;
    cube.cube_compatible = true;
    cube.array_layers = 6u;
    RHITextureViewDesc cube_view = color_sampled_view;
    cube_view.dimension = RHITextureViewDimension::TextureCube;
    cube_view.subresources.layer_count = 6u;
    check(validate_texture_desc(cube).succeeded() && validate_texture_view_desc(cube, cube_view).succeeded(),
          "Single Cube requires explicitly compatible six-layer storage");
    auto invalid_cube = cube;
    invalid_cube.cube_compatible = false;
    check(!validate_texture_view_desc(invalid_cube, cube_view),
          "Cube view must not reinterpret ordinary array storage");
    invalid_cube = cube;
    invalid_cube.width = 32u;
    check(!validate_texture_desc(invalid_cube), "Non-square Cube storage was accepted");
    invalid_cube = cube;
    invalid_cube.array_layers = 5u;
    check(!validate_texture_desc(invalid_cube), "Incomplete Cube storage was accepted");
    invalid_cube = cube;
    invalid_cube.sample_count = 2u;
    check(!validate_texture_desc(invalid_cube), "Multisample Cube storage was accepted");
    cube.array_layers = 18u;
    cube_view.dimension = RHITextureViewDimension::TextureCubeArray;
    cube_view.subresources.layer_count = 12u;
    cube_view.subresources.first_layer = 6u;
    check(validate_texture_view_desc(cube, cube_view).succeeded(), "Aligned Cube array groups must validate");
    cube_view.subresources.first_layer = 1u;
    check(!validate_texture_view_desc(cube, cube_view), "Unaligned Cube array first layer was accepted");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RHI format tests passed\n";
    return 0;
}
