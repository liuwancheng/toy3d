#include "drivers/rhi/rhi.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

#include "platform/platform_defines.h"

#if WITH_WIN
#include <windows.h>
#endif

#include "drivers/vulkan/vulkan_device.h"
#include "rendercore/geometry/gpu_skin_vertex_factory.h"
#include "rendercore/geometry/bone_matrix_buffer.h"
#include "rendercore/geometry/skeletal_mesh_render_data.h"
#include "rendercore/render_resource_manager.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_parameters.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"

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

    void check_status(const toy3d::RHIStatus& status)
    {
        check(static_cast<bool>(status), status.message().c_str());
    }

    void test_skeletal_resources(toy3d::RHIDevice& device, std::uint32_t num_bone_influences)
    {
        using namespace toy3d;
        SkeletalMeshAssetGeometry geometry;
        for (const Vector3 point : {Vector3(-0.75f, -0.5f, 0), Vector3(-0.25f, -0.5f, 0), Vector3(-0.5f, 0.5f, 0)})
        {
            StaticMeshAssetVertex vertex;
            vertex.position = point;
            vertex.normal = Vector3(1, 1, 0);
            geometry.mesh.vertices.push_back(vertex);
            geometry.skin_weights.push_back({{0, 1, 0, 0}, {127, 128, 0, 0}});
        }
        geometry.num_bone_influences = num_bone_influences;
        if (num_bone_influences == 8)
        {
            geometry.skin_weights.assign(3, {{0, 1, 2, 3, 4, 5, 6, 7}, {32, 32, 32, 31, 32, 32, 32, 32}});
        }
        geometry.mesh.indices = {0, 1, 2};
        geometry.mesh.sections = {{0, 3, 0}};
        geometry.mesh.material_slots = {"Default"};
        geometry.section_bone_maps = {{0, 1}};
        geometry.inverse_bind_matrices = {Matrix4(), Matrix4()};
        geometry.bone_local_bounds = {{true, Vector3(-0.75f, -0.5f, 0), Vector3(-0.25f, 0.5f, 0)},
                                      {true, Vector3(-0.75f, -0.5f, 0), Vector3(-0.25f, 0.5f, 0)}};
        if (num_bone_influences == 8)
        {
            geometry.section_bone_maps = {{0, 1, 2, 3, 4, 5, 6, 7}};
            geometry.inverse_bind_matrices.assign(8, Matrix4());
            const auto bounds = geometry.bone_local_bounds[0];
            geometry.bone_local_bounds.assign(8, bounds);
        }
        const std::vector<Vector4> rows{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0},
                                        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
        RenderResourceManager manager(device);
        SkeletalMeshRenderData mesh(geometry);
        BoneMatrixBuffer bones(rows);
        check_status(mesh.begin_init(manager));
        check_status(manager.begin_init(bones));
        {
            auto context = device.create_graphics_command_context();
            check(static_cast<bool>(context), context.status().message().c_str());
            check_status(context.value()->begin_recording("Discard skeletal uploads"));
            check_status(manager.record_pending_uploads(*context.value()));
            check_status(mesh.prepare_current_recording());
            std::weak_ptr<RHIBufferView> discarded_view = bones.view();
            check(!discarded_view.expired() && mesh.is_drawable(), "skeletal current-recording candidate");
            const auto list = context.value()->finish_recording();
            check(static_cast<bool>(list), list.status().message().c_str());
            check_status(manager.discard_recording());
            check(discarded_view.expired() && !mesh.is_drawable() && !bones.view() &&
                      bones.state() == RenderResourceState::PendingUpload,
                  "discard must withdraw GPU candidates and retain retryable CPU payload");
        }
        std::weak_ptr<RHIBuffer> submitted_index;
        std::uint64_t completion = 0;
        {
            auto context = device.create_graphics_command_context();
            check(static_cast<bool>(context), context.status().message().c_str());
            check_status(context.value()->begin_recording("Retry skeletal uploads"));
            check_status(manager.record_pending_uploads(*context.value()));
            check_status(mesh.prepare_current_recording());
            check(mesh.is_drawable() && bones.view() && mesh.num_bone_influences() == num_bone_influences &&
                      mesh.vertex_factory()->num_bone_influences() == num_bone_influences,
                  "skeletal discard retry rebuilds resources and preserves influence width");
            submitted_index = mesh.index_buffer_binding().buffer;
            const auto list = context.value()->finish_recording();
            check(static_cast<bool>(list), list.status().message().c_str());
            RHISubmitInfo submit;
            submit.command_lists.push_back(list.value());
            const auto submitted = device.graphics_queue().submit(submit);
            check(static_cast<bool>(submitted), submitted.status().message().c_str());
            completion = submitted.value().completion_value;
            check_status(manager.commit_recording());
            check(bones.state() == RenderResourceState::Ready, "bone upload ready requires successful submit");
            check_status(mesh.release(manager));
            check_status(manager.release(bones));
        }
        check(!submitted_index.expired(), "queue must retain released skeletal geometry until completion");
        check_status(device.graphics_queue().wait_for_value(completion));
        check(submitted_index.expired(), "completed skeletal upload may release backing buffers");
        std::cout << "Skeletal resources influence count " << num_bone_influences
                  << " discard/retry/completion passed\n";
    }
} // namespace

int main()
{
    using namespace toy3d;
#if WITH_WIN
    // A hidden native surface is needed for device selection; rendering and submission
    // below are entirely device-level and never acquire a swapchain image.
    HWND window = CreateWindowExW(0, L"STATIC", L"Toy3d typed buffer test", WS_OVERLAPPED, 0, 0, 32, 32, nullptr,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window != nullptr, "hidden surface creation");
    RHISurfaceDesc surface;
    surface.platform = RHISurfacePlatform::Win32;
    surface.window_handle = window;
    surface.application_handle = GetModuleHandleW(nullptr);
    VulkanDevice device;
    RHIDeviceDesc device_desc;
    device_desc.primary_surface = std::make_shared<RHISurface>(surface);
    device_desc.enable_validation = true;
    check_status(device.initialize(device_desc));
    Sha256Hash shared_skin_shader_key{};
    for (const std::uint32_t num_bone_influences : {0u, 4u, 8u})
    {
        const bool gpu_skin = num_bone_influences != 0;
        ShaderMapEntryLoader loader(
            PhysicalPath(std::string(TOY3D_TYPED_BUFFER_SHADER_ROOT) + (gpu_skin ? "/gpu_skin" : "")));
        ShaderMapProgramKey key;
        key.shader_name = gpu_skin ? "Toy3d/Test/GPUSkin" : "Toy3d/Test/TypedBuffer";
        key.pass_name = gpu_skin ? "GPUSkin" : "TypedBuffer";
        const auto loaded = loader.load_program(key);
        check(loaded.succeeded(), loaded.error.c_str());
        const auto typed_binding = std::find_if(loaded.program->bindings.begin(), loaded.program->bindings.end(),
                                                [](const ShaderMapBinding& binding)
                                                {
                                                    return binding.type == RHIResourceBindingType::ReadOnlyTypedBuffer;
                                                });
        check(typed_binding != loaded.program->bindings.end() &&
                  loaded.program->bindings.size() == (gpu_skin ? 2u : 1u),
              "shared skin shader reflects typed bones and one Object constant buffer");
        if (num_bone_influences == 4)
        {
            shared_skin_shader_key = loaded.program->stages.front().content_hash;
        }
        else if (num_bone_influences == 8)
        {
            check(shared_skin_shader_key == loaded.program->stages.front().content_hash,
                  "four/eight influence draws must use the exact same vertex shader bytecode");
        }
        const auto candidate = ShaderMap::create_candidate(*loaded.program, key);
        check(candidate.succeeded(), candidate.error.c_str());
        RHIShaderProgramCache programs(device);
        const auto program = programs.find_or_create(candidate.program);
        check(static_cast<bool>(program), program.status().message().c_str());
        RHITextureDesc target_desc;
        target_desc.width = 8;
        target_desc.height = 8;
        target_desc.format = PixelFormat::R8G8B8A8UNorm;
        target_desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::CopySource;
        target_desc.initial_access = RHIAccess::Common;
        const auto target = device.create_texture(target_desc);
        check(static_cast<bool>(target), target.status().message().c_str());
        RHITextureViewDesc target_view_desc;
        target_view_desc.type = RHIResourceViewType::RenderTarget;
        target_view_desc.format = target_desc.format;
        const auto target_view = device.create_texture_view(target.value(), target_view_desc);
        check(static_cast<bool>(target_view), target_view.status().message().c_str());
        const auto readback = device.create_texture_readback(target_desc.format, {8, 8}, "Typed VS readback");
        check(static_cast<bool>(readback), readback.status().message().c_str());
        RHICommandListRef command_list;
        std::weak_ptr<RHIBufferView> old_view;
        {
            RHIBufferDesc desc;
            desc.size = gpu_skin ? (num_bone_influences == 8 ? 48u : 12u) * 16u : 4u * 16u;
            desc.usage =
                RHIResourceUsage::ShaderResource | RHIResourceUsage::TypedBuffer | RHIResourceUsage::CopyDestination;
            desc.initial_access = RHIAccess::Common;
            const auto buffer = device.create_buffer(desc);
            check(static_cast<bool>(buffer), buffer.status().message().c_str());
            RHIBufferViewDesc view_desc;
            view_desc.format = PixelFormat::R32G32B32A32Float;
            view_desc.size = desc.size;
            const auto view = device.create_buffer_view(buffer.value(), view_desc);
            check(static_cast<bool>(view), view.status().message().c_str());
            old_view = view.value();
            auto invalid_view = view_desc;
            invalid_view.offset = 16;
            invalid_view.size = 16;
            if (device.limits().typed_buffer_offset_alignment > 16)
            {
                check(!device.create_buffer_view(buffer.value(), invalid_view), "device texel offset alignment");
            }
            RHIBindingSetDesc binding_desc;
            binding_desc.group = RHIBindingGroup::Object;
            RHIBindingValue value;
            value.binding_id = typed_binding->parameter_id;
            value.buffer_view = view.value();
            binding_desc.bindings.push_back(value);
            RHIGraphicsPipelineDesc pipeline_desc;
            pipeline_desc.vertex_shader = program.value()->vertex_shader;
            pipeline_desc.pixel_shader = program.value()->pixel_shader;
            pipeline_desc.binding_layout = program.value()->binding_layout;
            pipeline_desc.rasterization.cull_mode = RHICullMode::None;
            pipeline_desc.depth_stencil.depth_test_enable = false;
            pipeline_desc.depth_stencil.depth_write_enable = false;
            pipeline_desc.color_attachment_count = 1;
            pipeline_desc.color_formats[0] = target_desc.format;
            std::vector<RHIBufferRef> vertex_buffers;
            std::vector<RHIVertexBufferBinding> vertex_bindings;
            const std::array<float, 12> positions{-0.75f, -0.5f, 0, 1, -0.25f, -0.5f, 0, 1, -0.5f, 0.5f, 0, 1};
            const std::array<float, 18> attributes{1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0};
            const std::vector<std::uint8_t> vertex_skin =
                num_bone_influences == 8
                    ? std::vector<std::uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 32, 32, 32, 31, 32, 32, 32, 32}
                    : std::vector<std::uint8_t>{0, 1, 0, 0, 127, 128, 0, 0};
            std::vector<std::uint8_t> weights;
            for (std::size_t vertex = 0; vertex < 3; ++vertex)
            {
                weights.insert(weights.end(), vertex_skin.begin(), vertex_skin.end());
            }
            if (gpu_skin)
            {
                for (const auto bytes : {sizeof(positions), sizeof(attributes), weights.size()})
                {
                    RHIBufferDesc vertex_desc;
                    vertex_desc.size = bytes;
                    vertex_desc.usage = RHIResourceUsage::VertexBuffer | RHIResourceUsage::CopyDestination;
                    vertex_desc.initial_access = RHIAccess::Common;
                    const auto created = device.create_buffer(vertex_desc);
                    check(static_cast<bool>(created), created.status().message().c_str());
                    vertex_buffers.push_back(created.value());
                }
                const auto stride = num_bone_influences * 2u;
                std::vector<VertexStreamComponent> components{
                    {ShaderVertexAttributeId::Position0, 0, 0, 16, PixelFormat::R32G32B32A32Float, vertex_buffers[0]},
                    {ShaderVertexAttributeId::Normal0, 1, 0, 24, PixelFormat::R32G32B32A32Float, vertex_buffers[1]},
                    {ShaderVertexAttributeId::TexCoord0, 1, 16, 24, PixelFormat::R32G32Float, vertex_buffers[1]},
                    {ShaderVertexAttributeId::BlendIndices0, 2, 0, stride, PixelFormat::R8G8B8A8UInt,
                     vertex_buffers[2]},
                    {ShaderVertexAttributeId::BlendWeights0, 2, num_bone_influences, stride, PixelFormat::R8G8B8A8UNorm,
                     vertex_buffers[2]}};
                if (num_bone_influences == 8)
                {
                    components.push_back({ShaderVertexAttributeId::BlendIndices1, 2, 4, stride,
                                          PixelFormat::R8G8B8A8UInt, vertex_buffers[2]});
                    components.push_back({ShaderVertexAttributeId::BlendWeights1, 2, 12, stride,
                                          PixelFormat::R8G8B8A8UNorm, vertex_buffers[2]});
                }
                GPUSkinVertexFactory factory(std::move(components), num_bone_influences);
                check_status(factory.build_vertex_input(candidate.program->data().vertex_inputs,
                                                        pipeline_desc.vertex_buffers, pipeline_desc.vertex_attributes,
                                                        vertex_bindings));
            }
            const auto pipeline = device.create_graphics_pipeline(pipeline_desc);
            check(static_cast<bool>(pipeline), pipeline.status().message().c_str());
            auto context = device.create_graphics_command_context();
            check(static_cast<bool>(context), context.status().message().c_str());
            check_status(context.value()->begin_recording("Typed buffer VS load"));
            if (gpu_skin)
            {
                ObjectShaderParameters object;
                object.toy_num_bone_influences = num_bone_influences;
                const auto constants = create_transient_shader_binding(device, *context.value(), object);
                check(static_cast<bool>(constants), constants.status().message().c_str());
                const auto& values = constants.value()->desc().bindings;
                binding_desc.bindings.insert(binding_desc.bindings.end(), values.begin(), values.end());
            }
            const auto binding = device.create_binding_set(binding_desc);
            check(static_cast<bool>(binding), binding.status().message().c_str());
            check_status(context.value()->transition_resources(
                {{buffer.value(), {}, RHIAccess::Common, RHIAccess::CopyDestination},
                 {target.value(), {}, RHIAccess::Common, RHIAccess::RenderTarget}}));
            std::vector<float> texels;
            if (gpu_skin)
            {
                const std::vector<float> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                                                  1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
                const std::vector<float> transformed{2,    0, 0, 1.5f, 0, 1, 0, 0, 0, 0, 1, 0,
                                                     0.5f, 0, 0, 0,    0, 1, 0, 0, 0, 0, 1, 0};
                const std::uint32_t bone_count = num_bone_influences == 8 ? 8 : 2;
                for (std::uint32_t bone = 0; bone < bone_count; ++bone)
                {
                    const auto& rows = bone < bone_count / 2 ? identity : transformed;
                    texels.insert(texels.end(), rows.begin(), rows.end());
                }
            }
            else
            {
                texels = {-1, -1, 0, 1, 3, -1, 0, 1, -1, 3, 0, 1, 0.25f, 0.5f, 0.75f, 1};
            }
            RHIBufferUploadDesc upload;
            upload.destination = buffer.value();
            upload.source.data = texels.data();
            upload.source.size = texels.size() * sizeof(float);
            check_status(context.value()->upload_buffer(upload));
            check_status(context.value()->transition_resources(
                {{buffer.value(), {}, RHIAccess::CopyDestination, RHIAccess::ShaderResourceGraphics}}));
            if (gpu_skin)
            {
                const void* data[] = {positions.data(), attributes.data(), weights.data()};
                const std::size_t bytes[] = {sizeof(positions), sizeof(attributes), weights.size()};
                for (std::size_t i = 0; i < vertex_buffers.size(); ++i)
                {
                    check_status(context.value()->transition_resources(
                        {{vertex_buffers[i], {}, RHIAccess::Common, RHIAccess::CopyDestination}}));
                    RHIBufferUploadDesc vertex_upload;
                    vertex_upload.destination = vertex_buffers[i];
                    vertex_upload.source.data = data[i];
                    vertex_upload.source.size = bytes[i];
                    check_status(context.value()->upload_buffer(vertex_upload));
                    check_status(context.value()->transition_resources(
                        {{vertex_buffers[i], {}, RHIAccess::CopyDestination, RHIAccess::VertexBuffer}}));
                }
            }
            RHIRenderPassDesc pass;
            RHIColorAttachmentDesc color;
            color.view = target_view.value();
            color.load = RHILoadOperation::Clear;
            color.clear_value = RHIClearValue::Black;
            pass.color_attachments.push_back(color);
            check_status(context.value()->begin_render_pass(pass));
            check_status(context.value()->set_graphics_pipeline(pipeline.value()));
            if (gpu_skin)
            {
                check_status(context.value()->set_vertex_buffers(vertex_bindings));
            }
            check_status(context.value()->set_viewport({0, 0, 8, 8, 0, 1}));
            check_status(context.value()->set_scissor({0, 0, 8, 8}));
            RHIGraphicsBindings bindings;
            bindings.object = binding.value();
            check_status(context.value()->bind_graphics_bindings(bindings));
            check_status(context.value()->draw({3, 1, 0, 0}));
            check_status(context.value()->end_render_pass());
            check_status(context.value()->transition_resources(
                {{target.value(), {}, RHIAccess::RenderTarget, RHIAccess::CopySource}}));
            RHITextureReadbackDesc read;
            read.source.texture = target.value();
            read.extent = {8, 8};
            read.destination = readback.value();
            check_status(context.value()->readback_texture(read));
            const auto finished = context.value()->finish_recording();
            check(static_cast<bool>(finished), finished.status().message().c_str());
            command_list = finished.value();
        }
        check(!old_view.expired(), "recorded list must retain typed view after caller releases it");
        RHISubmitInfo submit;
        submit.command_lists.push_back(command_list);
        const auto submitted = device.graphics_queue().submit(submit);
        check(static_cast<bool>(submitted), submitted.status().message().c_str());
        submit.command_lists.clear();
        command_list.reset();
        check(!old_view.expired(), "submitted queue must retain typed view until completion");
        check_status(device.graphics_queue().wait_for_value(submitted.value().completion_value));
        const auto pixels = readback.value()->read_texture(device.graphics_queue().completed_value());
        check(static_cast<bool>(pixels), pixels.status().message().c_str());
        const std::size_t center = 4 * pixels.value().row_pitch + 4 * 4;
        std::cout << "VS typed-buffer influence count " << num_bone_influences
                  << " center RGBA: " << static_cast<unsigned>(pixels.value().bytes[center]) << ','
                  << static_cast<unsigned>(pixels.value().bytes[center + 1]) << ','
                  << static_cast<unsigned>(pixels.value().bytes[center + 2]) << ','
                  << static_cast<unsigned>(pixels.value().bytes[center + 3]) << '\n';
        if (gpu_skin)
        {
            // In both widths, identity bones have total weight 127/255 and scaled
            // bones total 128/255. Eight slots distribute these across eight distinct
            // bones, including all four extra inputs, producing (0.5995, 0.8004, 0).
            // The bind triangle was left
            // of center; its skinned positions must cover this pixel instead.
            check(pixels.value().bytes[center] >= 152 && pixels.value().bytes[center] <= 154 &&
                      pixels.value().bytes[center + 1] >= 203 && pixels.value().bytes[center + 1] <= 205 &&
                      pixels.value().bytes[center + 2] == 0,
                  "GPU skin position and nonuniform-scale normal differ from CPU oracle");
            const auto old_position = 4 * pixels.value().row_pitch + 1 * 4;
            check(pixels.value().bytes[old_position] == 0 && pixels.value().bytes[old_position + 1] == 0,
                  "GPU skin must move geometry away from the bind position");
            const auto outside_triangle = pixels.value().row_pitch + 4 * 4;
            check(pixels.value().bytes[outside_triangle] == 0 && pixels.value().bytes[outside_triangle + 1] == 0,
                  "four-slot aliases must not be accumulated twice by the shared shader");
        }
        else
        {
            check(pixels.value().bytes[center] >= 63 && pixels.value().bytes[center] <= 64 &&
                      pixels.value().bytes[center + 1] >= 127 && pixels.value().bytes[center + 1] <= 128 &&
                      pixels.value().bytes[center + 2] >= 191 && pixels.value().bytes[center + 2] <= 192,
                  "VS formatted buffer Load produced the wrong color");
        }
        check(old_view.expired(), "completed list may release typed view");
        programs.clear();
    }
    test_skeletal_resources(device, 4);
    test_skeletal_resources(device, 8);
    check_status(device.shutdown());
    check(DestroyWindow(window) != FALSE, "hidden surface destruction");
    std::cout << "Vulkan typed buffer VS read and completion lifetime passed\n";
#endif
    return 0;
}
