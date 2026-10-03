#include "drivers/rhi/rhi.h"
#include "rendercore/shader/shader_parameters.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"
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
} // namespace

void test_skin_tangent_frames(toy3d::RHIDevice& device)
{
    using namespace toy3d;
    ShaderMapEntryLoader loader(PhysicalPath(std::string(TOY3D_TYPED_BUFFER_SHADER_ROOT) + "/skin_tangent"));
    ShaderMapProgramKey key;
    key.shader_name = "Toy3d/Test/SkinTangent";
    key.pass_name = "SkinTangent";
    const auto loaded = loader.load_program(key);
    check(loaded.succeeded(), loaded.error.c_str());
    const auto candidate = ShaderMap::create_candidate(*loaded.program, key);
    check(candidate.succeeded(), candidate.error.c_str());
    RHIShaderProgramCache programs(device);
    const auto program = programs.find_or_create(candidate.program);
    check(static_cast<bool>(program), program.status().message().c_str());
    // Both influence widths share this exact Program; only Object data and buffer contents vary.
    const auto vs_identity = candidate.program->data().stages.front().content_hash;
    for (const std::uint32_t influences : {4u, 8u})
    {
        check(candidate.program->data().stages.front().content_hash == vs_identity,
              "Influence widths share VS bytecode");
        RHITextureDesc output;
        output.width = 16;
        output.height = 6;
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
        const auto readback = device.create_texture_readback(output.format, {16, 6}, "Skin frame IEEE float readback");
        check(static_cast<bool>(readback), readback.status().message().c_str());
        RHIBufferDesc bone_desc;
        bone_desc.size = influences * 6u * 16u;
        bone_desc.usage =
            RHIResourceUsage::TypedBuffer | RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
        bone_desc.initial_access = RHIAccess::Common;
        const auto bone_buffer = device.create_buffer(bone_desc);
        check(static_cast<bool>(bone_buffer), bone_buffer.status().message().c_str());
        RHIBufferViewDesc bone_view_desc;
        bone_view_desc.format = PixelFormat::R32G32B32A32Float;
        bone_view_desc.size = bone_desc.size;
        const auto bone_view = device.create_buffer_view(bone_buffer.value(), bone_view_desc);
        check(static_cast<bool>(bone_view), bone_view.status().message().c_str());
        const auto context = device.create_graphics_command_context();
        check(static_cast<bool>(context), context.status().message().c_str());
        status(context.value()->begin_recording("Skin frame under nonuniform and negative scale"));
        std::vector<float> rows;
        const std::vector<float> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        const std::vector<float> scaled{2,    0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 3,           0,
                                        0.5f, 0, 0, 0, 0, 2,    0, 0, 0, 0, 1.0f / 3.0f, 0};
        for (std::uint32_t bone = 0u; bone < influences; ++bone)
        {
            const auto& source = bone < influences / 2u ? identity : scaled;
            rows.insert(rows.end(), source.begin(), source.end());
        }
        status(context.value()->transition_resources(
            {{bone_buffer.value(), {}, RHIAccess::Common, RHIAccess::CopyDestination}}));
        RHIBufferUploadDesc upload;
        upload.destination = bone_buffer.value();
        upload.source.data = rows.data();
        upload.source.size = rows.size() * sizeof(float);
        status(context.value()->upload_buffer(upload));
        status(context.value()->transition_resources(
            {{bone_buffer.value(), {}, RHIAccess::CopyDestination, RHIAccess::ShaderResourceGraphics}}));
        ObjectShaderParameters object;
        object.toy_num_bone_influences = influences;
        object.toy_object_to_world = Matrix4::identity();
        object.toy_object_to_world.at(0, 0) = -2.0f;
        object.toy_object_to_world.at(1, 1) = 3.0f;
        object.toy_object_to_world.at(2, 2) = 0.5f;
        object.toy_object_normal_to_world = Matrix4::identity();
        object.toy_object_normal_to_world.at(0, 0) = -0.5f;
        object.toy_object_normal_to_world.at(1, 1) = 1.0f / 3.0f;
        object.toy_object_normal_to_world.at(2, 2) = 2.0f;
        const auto constants = create_transient_shader_binding(device, *context.value(), object);
        check(static_cast<bool>(constants), constants.status().message().c_str());
        RHIBindingSetDesc bindings_desc;
        bindings_desc.group = RHIBindingGroup::Object;
        bindings_desc.bindings = constants.value()->desc().bindings;
        const auto found = std::find_if(loaded.program->bindings.begin(), loaded.program->bindings.end(),
                                        [](const ShaderMapBinding& binding)
                                        {
                                            return binding.name == "bone_matrix_buffer";
                                        });
        check(found != loaded.program->bindings.end(), "Skin frame typed buffer reflects");
        RHIBindingValue value;
        value.binding_id = found->parameter_id;
        value.buffer_view = bone_view.value();
        bindings_desc.bindings.push_back(value);
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
        status(
            context.value()->transition_resources({{target.value(), {}, RHIAccess::Common, RHIAccess::RenderTarget}}));
        RHIRenderPassDesc pass;
        RHIColorAttachmentDesc color;
        color.view = target_view.value();
        color.load = RHILoadOperation::Clear;
        color.clear_value = RHIClearValue::Black;
        pass.color_attachments.push_back(color);
        status(context.value()->begin_render_pass(pass));
        status(context.value()->set_graphics_pipeline(pipeline.value()));
        status(context.value()->set_viewport({0, 0, 16, 6, 0, 1}));
        status(context.value()->set_scissor({0, 0, 16, 6}));
        RHIGraphicsBindings bindings;
        bindings.object = binding_set.value();
        status(context.value()->bind_graphics_bindings(bindings));
        status(context.value()->draw({3, 1, 0, 0}));
        status(context.value()->end_render_pass());
        status(context.value()->transition_resources(
            {{target.value(), {}, RHIAccess::RenderTarget, RHIAccess::CopySource}}));
        RHITextureReadbackDesc read;
        read.source.texture = target.value();
        read.extent = {16, 6};
        read.destination = readback.value();
        status(context.value()->readback_texture(read));
        const auto list = context.value()->finish_recording();
        check(static_cast<bool>(list), list.status().message().c_str());
        RHISubmitInfo submit;
        submit.command_lists.push_back(list.value());
        const auto submitted = device.graphics_queue().submit(submit);
        check(static_cast<bool>(submitted), submitted.status().message().c_str());

        status(device.graphics_queue().wait_for_value(submitted.value().completion_value));
        const auto pixels = readback.value()->read_texture(device.graphics_queue().completed_value());
        check(static_cast<bool>(pixels), pixels.status().message().c_str());
        for (unsigned y = 0; y < 6; ++y)
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
                // Analytic oracle: inverse-transpose N=(-.375,.5,0), linear T=(-3,-2.25,0),
                // B=(0,0,-1). Unit N=(-.6,.8,0), T=(-.8,-.6,0), orientation=-1.
                constexpr double expected_rows[] = {-0.6, 0.8, -0.8, -0.6, -1.0, -1.0};
                const double expected = expected_rows[y];
                if (!std::isfinite(actual) ||
                    std::abs(actual - expected) > std::max(0.00001, std::abs(expected) * 0.0001))
                {
                    std::cerr << "Skin tangent oracle mismatch (" << x << ',' << y << "): " << actual << " expected "
                              << expected << '\n';
                    check(false, "GPU tangent frame differs from the independent analytic oracle");
                }
            }
        }
    }
    programs.clear();
    std::cout << "Skin tangent frame verified under bone/object nonuniform and negative scale\n";
}
