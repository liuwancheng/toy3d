#include "renderscene/renderer.h"

#include <exception>
#include <utility>

#include "drivers/rhi/rhi_device.h"
#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/geometry/gpu_skin_vertex_factory.h"
#include "rendercore/render_command.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/shader/shader_parameters.h"
#include "renderscene/pass/hit_proxy_pass.h"
#include "renderscene/postprocess/tonemap_pass.h"
#include "renderscene/ui/imgui_renderer.h"
#include "shader_parameters/toy3d_shadowdepth_default.generated.h"
#include "shader_parameters/toy3d_editor_hitproxy.generated.h"
#include "renderscene/scene_render_targets.h"

namespace toy3d
{
    RHIStatus Renderer::validate_mesh_shader(const ShaderMapProgramRef& program, const VertexFactory* geometry)
    {
        if (!program || program->data().contract.usage == shader::ShaderUsage::Global)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Mesh validation requires a declared mesh Program.");
        }
        const auto& data = program->data();
        const bool shadow = data.contract.role == shader::ShaderPassRole::ShadowDepth;
        const bool hit = data.contract.role == shader::ShaderPassRole::HitProxy;
        if (shadow || hit)
        {
            const ShadowDepthPassParameters shadow_parameters;
            const HitProxyPassParameters hit_parameters;
            const auto& metadata =
                shadow ? shader_parameters_metadata(shadow_parameters) : shader_parameters_metadata(hit_parameters);
            const auto status = validate_shader_parameters_group_against_schema(metadata, data.parameter_schema);
            if (!status)
            {
                return RHIStatus::failure(status.code(),
                                          status.message() + " Rebuild Editor if generated parameters changed.");
            }
        }
        auto shader = shader_program_cache_->find_or_create(program);
        if (!shader)
        {
            return shader.status();
        }
        std::unique_ptr<VertexFactory> factory;
        // VertexFactory borrows streams; preflight owns their buffers until input construction completes.
        std::vector<RHIBufferRef> representative_buffers;
        if (!geometry)
        {
            RHIBufferDesc desc;
            desc.usage = RHIResourceUsage::VertexBuffer;
            desc.size = 16u;
            auto position = device_->create_buffer(desc);
            desc.size = 24u;
            auto surface = device_->create_buffer(desc);
            desc.size = 4u;
            auto attributes = device_->create_buffer(desc);
            if (!position || !surface || !attributes)
            {
                return !position ? position.status() : (!surface ? surface.status() : attributes.status());
            }
            representative_buffers = {position.value(), surface.value(), attributes.value()};
            std::vector<VertexStreamComponent> components = {
                {ShaderVertexAttributeId::Position0, 0u, 0u, 16u, PixelFormat::R32G32B32A32Float, position.value()},
                {ShaderVertexAttributeId::Normal0, 1u, 0u, 24u, PixelFormat::R32G32B32A32Float, surface.value()},
                {ShaderVertexAttributeId::TexCoord0, 1u, 16u, 24u, PixelFormat::R32G32Float, surface.value()},
                {ShaderVertexAttributeId::Color0, 2u, 0u, 4u, PixelFormat::R8G8B8A8UNorm, attributes.value()}};
            if (data.contract.vertex_factory == shader::VertexFactoryType::Local)
            {
                factory = std::make_unique<LocalVertexFactory>(std::move(components));
            }
            else if (data.contract.vertex_factory == shader::VertexFactoryType::GPUSkin)
            {
                desc.size = 16u;
                const auto skin = device_->create_buffer(desc);
                if (!skin)
                {
                    return skin.status();
                }
                representative_buffers.push_back(skin.value());
                components.push_back(
                    {ShaderVertexAttributeId::BlendIndices0, 3u, 0u, 16u, PixelFormat::R8G8B8A8UInt, skin.value()});
                components.push_back(
                    {ShaderVertexAttributeId::BlendIndices1, 3u, 4u, 16u, PixelFormat::R8G8B8A8UInt, skin.value()});
                components.push_back(
                    {ShaderVertexAttributeId::BlendWeights0, 3u, 8u, 16u, PixelFormat::R8G8B8A8UNorm, skin.value()});
                components.push_back(
                    {ShaderVertexAttributeId::BlendWeights1, 3u, 12u, 16u, PixelFormat::R8G8B8A8UNorm, skin.value()});
                factory = std::make_unique<GPUSkinVertexFactory>(std::move(components), 8u);
            }
            else
            {
                return RHIStatus::failure(RHIErrorCode::Unsupported,
                                          "Mesh Program declares an unsupported VertexFactory.");
            }
            geometry = factory.get();
        }
        RHIGraphicsPipelineDesc pipeline;
        std::vector<RHIVertexBufferBinding> bindings;
        auto status = geometry->build_vertex_input(data.vertex_inputs, pipeline.vertex_buffers,
                                                   pipeline.vertex_attributes, bindings);
        if (!status)
        {
            return status;
        }
        pipeline.vertex_shader = shader.value()->vertex_shader;
        pipeline.pixel_shader = shader.value()->pixel_shader;
        pipeline.binding_layout = shader.value()->binding_layout;
        pipeline.color_attachment_count = shadow ? 0u : 1u;
        pipeline.depth_stencil_format = PixelFormat::D32Float;
        if (hit)
        {
            pipeline.color_formats[0] = PixelFormat::R32UInt;
        }
        else if (!shadow)
        {
            if (!scene_render_targets_ || !scene_render_targets_->scene_color_view() ||
                !scene_render_targets_->scene_depth_view())
            {
                return RHIStatus::failure(RHIErrorCode::NotReady,
                                          "Scene attachments are not ready for material validation.");
            }
            pipeline.color_formats[0] = scene_render_targets_->scene_color_view()->desc().format;
            pipeline.depth_stencil_format = scene_render_targets_->scene_depth_view()->desc().format;
            pipeline.sample_count = scene_render_targets_->scene_color_texture()->desc().sample_count;
        }
        for (const bool two_sided : {false, true})
        {
            auto state = data.graphics_pass_state;
            if (two_sided)
            {
                state.cull_mode = shader::ShaderGraphicsPassState::CullMode::None;
            }
            auto configured = build_shader_graphics_pipeline_desc(pipeline, state);
            if (!configured)
            {
                return configured.status();
            }
            const auto created = device_->create_graphics_pipeline(configured.value());
            if (!created)
            {
                return created.status();
            }
        }
        return RHIStatus::success();
    }

    void Renderer::prepare_builtin_shaders(BuiltinShaderUpdateRef request)
    {
        enqueue_render_command(
            "PrepareBuiltinShaders",
            [this, request = std::move(request)]() noexcept
            {
                if (!request)
                {
                    return;
                }
                try
                {
                    request->status = [&]() -> RHIStatus
                    {
                        if (!device_ || !shader_program_cache_ ||
                            lifecycle_state_.load() != RendererLifecycleState::Running)
                        {
                            return RHIStatus::failure(RHIErrorCode::NotReady,
                                                      "Shader update requires a running Renderer.");
                        }
                        if (builtin_update_)
                        {
                            return RHIStatus::failure(RHIErrorCode::NotReady,
                                                      "Another builtin Shader candidate is pending.");
                        }
                        if (request->shader_maps.size() == 1u && request->shader_maps.front() &&
                            request->shader_maps.front()->programs().front()->data().contract.usage ==
                                shader::ShaderUsage::MeshPass)
                        {
                            const auto& collection = request->shader_maps.front();
                            const auto& name = collection->index().shader_name;
                            if (name != "Toy3d/ShadowDepth/Default" && name != "Toy3d/Editor/HitProxy")
                            {
                                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                                          "Unknown builtin mesh Pass source.");
                            }
                            const auto required_role = name == "Toy3d/ShadowDepth/Default"
                                                           ? shader::ShaderPassRole::ShadowDepth
                                                           : shader::ShaderPassRole::HitProxy;
                            for (const auto factory :
                                 {shader::VertexFactoryType::Local, shader::VertexFactoryType::GPUSkin})
                            {
                                const auto selected = collection->find(required_role, factory);
                                if (!selected.succeeded())
                                {
                                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, selected.error);
                                }
                            }
                            for (const auto& program : collection->programs())
                            {
                                const auto status = validate_mesh_shader(program);
                                if (!status)
                                {
                                    return status;
                                }
                            }
                            if (name == "Toy3d/ShadowDepth/Default")
                            {
                                pending_mesh_pass_programs_.shadow_depth_default = collection;
                            }
                            else
                            {
                                pending_mesh_pass_programs_.hit_proxy = collection;
                            }
                        }
                        else
                        {
                            std::vector<ShaderMapProgramRef> programs;
                            for (const auto& collection : request->shader_maps)
                            {
                                if (!collection || collection->programs().front()->data().contract.usage !=
                                                       shader::ShaderUsage::Global)
                                {
                                    return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                                              "Global replacement requires Global collections.");
                                }
                                programs.insert(programs.end(), collection->programs().begin(),
                                                collection->programs().end());
                            }
                            auto shaders = global_shader_map_input_->replace(programs);
                            if (!shaders.succeeded())
                            {
                                return RHIStatus::failure(RHIErrorCode::InvalidArgument, shaders.error);
                            }
                            RHIStatus status;
                            auto tonemap = std::make_unique<TonemapPassResources>();
                            status = tonemap->initialize(*device_, *shader_program_cache_, *shaders.shader_map);
                            if (!status)
                            {
                                return status;
                            }
                            if (imgui_renderer_)
                            {
                                status = imgui_renderer_->prepare_shader(*device_, *shader_program_cache_,
                                                                         *shaders.shader_map);
                                if (!status)
                                {
                                    return status;
                                }
                            }
                            pending_tonemap_resources_ = std::move(tonemap);
                            pending_global_shaders_ = std::move(shaders.shader_map);
                        }
                        builtin_update_ = request;
                        return RHIStatus::success();
                    }();
                }
                catch (const std::exception& error)
                {
                    request->status = RHIStatus::failure(RHIErrorCode::BackendFailure, error.what());
                }
                if (!request->status)
                {
                    if (!builtin_update_)
                    {
                        pending_mesh_pass_programs_ = {};
                        pending_global_shaders_.reset();
                        pending_tonemap_resources_.reset();
                        if (imgui_renderer_)
                        {
                            imgui_renderer_->discard_shader();
                        }
                    }
                    request->resolved.store(true, std::memory_order_release);
                }
                request->prepared.store(true, std::memory_order_release);
            });
    }

    void Renderer::resolve_builtin_shaders()
    {
        if (!builtin_update_)
        {
            return;
        }
        const auto decision = builtin_update_->decision.load(std::memory_order_acquire);
        if (decision == BuiltinShaderDecision::Pending)
        {
            return;
        }
        if (decision == BuiltinShaderDecision::Commit && lifecycle_state_.load() == RendererLifecycleState::Running)
        {
            if (pending_global_shaders_)
            {
                // Only owned strong refs change between frames. Backend commands
                // keep all resources of previous submissions alive until completion.
                global_shader_map_input_ = std::move(pending_global_shaders_);
                tonemap_pass_resources_ = std::move(pending_tonemap_resources_);
                if (imgui_renderer_)
                {
                    imgui_renderer_->publish_shader();
                }
            }
            if (pending_mesh_pass_programs_.shadow_depth_default)
            {
                mesh_pass_programs_input_.shadow_depth_default =
                    std::move(pending_mesh_pass_programs_.shadow_depth_default);
            }
            if (pending_mesh_pass_programs_.hit_proxy)
            {
                mesh_pass_programs_input_.hit_proxy = std::move(pending_mesh_pass_programs_.hit_proxy);
            }
            builtin_update_->applied = true;
        }
        if (imgui_renderer_)
        {
            imgui_renderer_->discard_shader();
        }
        pending_global_shaders_.reset();
        pending_tonemap_resources_.reset();
        pending_mesh_pass_programs_ = {};
        builtin_update_->resolved.store(true, std::memory_order_release);
        builtin_update_.reset();
    }
} // namespace toy3d
