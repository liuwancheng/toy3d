#include "drivers/rhi/rhi.h"

#include <cstdlib>
#include <iostream>

namespace
{
    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }
} // namespace

int main()
{
    using namespace toy3d;
    RHIBufferDesc buffer;
    buffer.size = 96 * 256;
    buffer.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::TypedBuffer | RHIResourceUsage::CopyDestination;
    RHIBufferViewDesc view;
    view.format = PixelFormat::R32G32B32A32Float;
    view.size = buffer.size;
    check(static_cast<bool>(validate_buffer_view_desc(buffer, view)), "six-row bone typed view");
    buffer.structure_stride = 16;
    check(!validate_buffer_desc(buffer), "typed buffer accepted structured stride");
    buffer.structure_stride = 0;
    buffer.usage |= RHIResourceUsage::UnorderedAccess;
    check(!validate_buffer_desc(buffer), "read-only typed buffer accepted UAV");
    buffer.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::TypedBuffer;
    view.offset = 1;
    view.size = 16;
    check(!validate_buffer_view_desc(buffer, view), "unaligned texel offset accepted");
    view.offset = 0;
    view.size = 17;
    check(!validate_buffer_view_desc(buffer, view), "partial texel accepted");
    view.size = 16;
    view.format = PixelFormat::Unknown;
    check(!validate_buffer_view_desc(buffer, view), "typed buffer accepted raw view");
    view.format = PixelFormat::BC1UNorm;
    check(!validate_buffer_view_desc(buffer, view), "compressed typed texel accepted");
    view.format = PixelFormat::R32G32B32A32Float;
    buffer.usage = RHIResourceUsage::ShaderResource;
    check(!validate_buffer_view_desc(buffer, view), "typed view accepted non-typed backing usage");
    RHITextureDesc texture;
    texture.format = PixelFormat::R32G32B32A32Float;
    texture.usage = RHIResourceUsage::TypedBuffer;
    check(!validate_texture_desc(texture), "texture accepted buffer-only usage");

    RHIBindingLayoutDesc layout;
    RHIBindingLayoutEntry bone;
    bone.binding_id = 1;
    bone.group = RHIBindingGroup::Object;
    bone.type = RHIResourceBindingType::ReadOnlyTypedBuffer;
    bone.stages = RHIShaderStageFlags::Vertex;
    layout.entries = {bone};
    RHILimits limits;
    limits.max_typed_buffer_elements = 1536;
    limits.max_sampled_resources_per_stage = 1;
    limits.max_sampled_resources_per_layout = 2;
    limits.max_resources_per_stage = 2;
    check(static_cast<bool>(validate_typed_binding_layout_limits(layout, limits)), "typed descriptor budget");
    auto sampled = bone;
    sampled.binding_id = 2;
    sampled.group = RHIBindingGroup::Material;
    sampled.type = RHIResourceBindingType::SampledTexture;
    layout.entries.push_back(sampled);
    check(!validate_typed_binding_layout_limits(layout, limits),
          "typed and texture stage budgets were counted separately");
    layout.entries.back().stages = RHIShaderStageFlags::Pixel;
    check(static_cast<bool>(validate_typed_binding_layout_limits(layout, limits)), "independent stage budgets");
    limits.max_sampled_resources_per_layout = 1;
    check(!validate_typed_binding_layout_limits(layout, limits), "combined layout sampled budget exceeded");
    limits.max_sampled_resources_per_layout = 2;
    limits.max_resources_per_stage = 0;
    check(!validate_typed_binding_layout_limits(layout, limits), "stage total resource budget exceeded");
    std::cout << "Typed buffer validation tests passed\n";
    return 0;
}
