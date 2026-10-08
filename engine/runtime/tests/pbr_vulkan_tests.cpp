#include "drivers/rhi/rhi.h"
#include "image/float16.h"
#include "rendercore/render_resource_manager.h"
#include "rendercore/texture/texture_resource.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
    void status(const toy3d::RHIStatus& value)
    {
        check(static_cast<bool>(value), value.message().c_str());
    }
    // Independent double reference: direct algebra and fixed fit coefficients, no engine math helpers.
    double reference_env(double nv)
    {
        const double a = std::min(0.25, std::pow(2.0, -9.28 * nv)) * 0.5 + 0.02875;
        return 0.04 * (-1.04 * a + 0.754) + 1.04 * a - 0.029;
    }
    double reference_ggx(double r, double nh)
    {
        const double a2 = std::pow(r, 4.0);
        const double denominator = 1.0 + nh * nh * (a2 - 1.0);
        return std::min(a2 / (3.14159265358979323846 * denominator * denominator), 2048.0);
    }
    double reference_value(unsigned x, unsigned y)
    {
        const double r = std::max(x / 15.0, 1.0 / 64.0);
        if (y == 0)
        {
            return reference_ggx(r, 1.0);
        }
        if (y == 1)
        {
            return reference_ggx(r, 0.5);
        }
        if (y == 2)
        {
            return reference_env(x / 15.0);
        }
        if (y == 3)
        {
            return 0.04 + 0.76 * x / 15.0;
        }
        if (y == 4)
        {
            const double cutoff = std::max(0.0, 1.0 - std::pow(x / 8.0, 4.0));
            return cutoff * cutoff / std::max(static_cast<double>(x * x), 0.0001);
        }
        if (y == 5)
        {
            return (x % 6 + 1.0) * (x < 6 ? 1.0 : x < 12 ? 1.5 : 4.0);
        }
        const double nv = x / 15.0;
        return 0.5 / 3.14159265358979323846 +
               reference_env(nv) * reference_ggx(0.5, std::sqrt((1.0 + nv) * 0.5)) * 0.375;
    }
} // namespace

void test_pbr_math_and_cube(toy3d::RHIDevice& device)
{
    using namespace toy3d;
    ShaderMapEntryLoader loader(PhysicalPath(std::string(TOY3D_TYPED_BUFFER_SHADER_ROOT) + "/pbr_validation"));
    ShaderMapProgramKey key;
    key.shader_name = "Toy3d/Test/PBRValidation";
    key.pass_name = "PBRValidation";
    const auto loaded = loader.load_program(key);
    check(loaded.succeeded(), loaded.error.c_str());
    const auto candidate = ShaderMap::create_candidate(*loaded.program, key);
    check(candidate.succeeded(), candidate.error.c_str());
    RHIShaderProgramCache programs(device);
    const auto program = programs.find_or_create(candidate.program);
    check(static_cast<bool>(program), program.status().message().c_str());
    TextureDesc cube_desc;
    cube_desc.width = cube_desc.height = 4u;
    cube_desc.format = PixelFormat::R16G16B16A16Float;
    cube_desc.usage = TextureUsage::LinearData;
    cube_desc.cube = cube_desc.requires_linear_filter = true;
    for (unsigned mip = 0; mip < 3; ++mip)
    {
        const unsigned size = 4u >> mip;
        cube_desc.row_pitches.push_back(size * 8u);
        cube_desc.slice_pitches.push_back(size * size * 8u);
        std::vector<std::uint8_t> bytes;
        for (unsigned face = 0; face < 6; ++face)
        {
            std::uint16_t encoded = 0;
            check(try_encode_float16(static_cast<float>((face + 1) * (1u << mip)), encoded),
                  "Cube fixture half encoding");
            for (unsigned pixel = 0; pixel < size * size; ++pixel)
            {
                for (unsigned component = 0; component < 4; ++component)
                {
                    const auto bits = component == 3 ? 0x3c00u : encoded;
                    bytes.push_back(static_cast<std::uint8_t>(bits & 255u));
                    bytes.push_back(static_cast<std::uint8_t>(bits >> 8u));
                }
            }
        }
        cube_desc.mip_pixels.push_back(std::move(bytes));
    }
    RenderResourceManager manager(device);
    TextureResource cube(cube_desc);
    status(manager.begin_init(cube));
    RHITextureDesc output;
    output.width = 16;
    output.height = 7;
    output.format = PixelFormat::R8G8B8A8UNorm;
    output.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::CopySource;
    output.initial_access = RHIAccess::Common;
    const auto target = device.create_texture(output);
    check(static_cast<bool>(target), target.status().message().c_str());
    RHITextureViewDesc view_desc;
    view_desc.type = RHIResourceViewType::RenderTarget;
    view_desc.format = output.format;
    const auto target_view = device.create_texture_view(target.value(), view_desc);
    check(static_cast<bool>(target_view), target_view.status().message().c_str());
    const auto readback = device.create_texture_readback(output.format, {16, 7}, "PBR IEEE float readback");
    check(static_cast<bool>(readback), readback.status().message().c_str());
    RHISamplerDesc sampler_desc;
    sampler_desc.address_u = sampler_desc.address_v = sampler_desc.address_w = RHIAddressMode::ClampToEdge;
    const auto sampler = device.create_sampler(sampler_desc);
    check(static_cast<bool>(sampler), sampler.status().message().c_str());
    const auto context = device.create_graphics_command_context();
    check(static_cast<bool>(context), context.status().message().c_str());
    status(context.value()->begin_recording("PBR math and real Cube sampling"));
    status(manager.record_pending_uploads(*context.value()));
    RHIBindingSetDesc bindings_desc;
    bindings_desc.group = RHIBindingGroup::Pass;
    for (const auto& binding : loaded.program->bindings)
    {
        RHIBindingValue value;
        value.binding_id = binding.parameter_id;
        if (binding.name == "validation_cube")
        {
            value.texture_view = cube.view_for_current_recording();
        }
        else if (binding.name == "validation_sampler")
        {
            value.sampler = sampler.value();
        }
        else
        {
            check(false, "Unexpected PBR validation active binding");
        }
        bindings_desc.bindings.push_back(value);
    }
    const auto binding_set = device.create_binding_set(bindings_desc);
    check(static_cast<bool>(binding_set), binding_set.status().message().c_str());
    RHIGraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = program.value()->vertex_shader;
    pipeline_desc.pixel_shader = program.value()->pixel_shader;
    pipeline_desc.binding_layout = program.value()->binding_layout;
    pipeline_desc.rasterization.cull_mode = RHICullMode::None;
    pipeline_desc.depth_stencil.depth_test_enable = false;
    pipeline_desc.depth_stencil.depth_write_enable = false;
    pipeline_desc.color_attachment_count = 1;
    pipeline_desc.color_formats[0] = output.format;
    const auto pipeline = device.create_graphics_pipeline(pipeline_desc);
    check(static_cast<bool>(pipeline), pipeline.status().message().c_str());
    status(context.value()->transition_resources({{target.value(), {}, RHIAccess::Common, RHIAccess::RenderTarget}}));
    RHIRenderPassDesc pass;
    RHIColorAttachmentDesc color;
    color.view = target_view.value();
    color.load = RHILoadOperation::Clear;
    color.clear_value = RHIClearValue::Black;
    pass.color_attachments.push_back(color);
    status(context.value()->begin_render_pass(pass));
    status(context.value()->set_graphics_pipeline(pipeline.value()));
    status(context.value()->set_viewport({0, 0, 16, 7, 0, 1}));
    status(context.value()->set_scissor({0, 0, 16, 7}));
    RHIGraphicsBindings bindings;
    bindings.pass = binding_set.value();
    status(context.value()->bind_graphics_bindings(bindings));
    status(context.value()->draw({3, 1, 0, 0}));
    status(context.value()->end_render_pass());
    status(
        context.value()->transition_resources({{target.value(), {}, RHIAccess::RenderTarget, RHIAccess::CopySource}}));
    RHITextureReadbackDesc read;
    read.source.texture = target.value();
    read.extent = {16, 7};
    read.destination = readback.value();
    status(context.value()->readback_texture(read));
    const auto list = context.value()->finish_recording();
    check(static_cast<bool>(list), list.status().message().c_str());
    RHISubmitInfo submit;
    submit.command_lists.push_back(list.value());
    const auto submitted = device.graphics_queue().submit(submit);
    check(static_cast<bool>(submitted), submitted.status().message().c_str());
    status(manager.commit_recording());
    status(device.graphics_queue().wait_for_value(submitted.value().completion_value));
    const auto pixels = readback.value()->read_texture(device.graphics_queue().completed_value());
    check(static_cast<bool>(pixels), pixels.status().message().c_str());
    for (unsigned y = 0; y < 7; ++y)
    {
        for (unsigned x = 0; x < 16; ++x)
        {
            const auto offset = y * pixels.value().row_pitch + x * 4u;
            std::uint32_t bits = 0;
            for (unsigned component = 0; component < 4; ++component)
            {
                bits |= static_cast<std::uint32_t>(pixels.value().bytes[offset + component]) << (component * 8u);
            }
            float actual = 0;
            std::memcpy(&actual, &bits, sizeof(actual));
            const double expected = reference_value(x, y);
            if (!std::isfinite(actual) || std::abs(actual - expected) > std::max(0.00001, std::abs(expected) * 0.0001))
            {
                std::cerr << "PBR oracle mismatch (" << x << ',' << y << "): " << actual << " expected " << expected
                          << '\n';
                check(false, "PBR/Cube GPU differs from independent CPU oracle");
            }
        }
    }
    status(manager.release(cube));
    programs.clear();
    std::cout << "112 PBR/Cube pixels verified against independent double reference\n";
}
