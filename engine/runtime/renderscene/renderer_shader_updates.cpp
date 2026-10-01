#include "renderscene/renderer.h"

#include <exception>
#include <utility>

#include "drivers/rhi/rhi_device.h"
#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/render_command.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/shader/shader_parameters.h"
#include "renderscene/pass/hit_proxy_pass.h"
#include "renderscene/postprocess/tonemap_pass.h"
#include "renderscene/ui/imgui_renderer.h"
#include "shader_parameters/toy3d_shadowdepth_default.generated.h"

namespace toy3d
{
    RHIStatus Renderer::validate_mesh_shader(const ShaderMapProgramRef& program, bool shadow)
    {
        if (shadow)
        {
            const ShadowDepthPassParameters parameters;
            const auto status = validate_shader_parameters_metadata_against_schema(
                shader_parameters_metadata(parameters), program->data().parameter_schema);
            if (!status) return RHIStatus::failure(status.code(), status.message() + " Rebuild Editor if generated parameters changed.");
        }
        auto shader = shader_program_cache_->find_or_create(program);
        if (!shader) return shader.status();
        RHIBufferDesc position_desc; position_desc.size = 16u; position_desc.usage = RHIResourceUsage::VertexBuffer;
        RHIBufferDesc surface_desc; surface_desc.size = 24u; surface_desc.usage = RHIResourceUsage::VertexBuffer;
        auto position = device_->create_buffer(position_desc);
        auto surface = device_->create_buffer(surface_desc);
        if (!position) return position.status();
        if (!surface) return surface.status();
        LocalVertexFactory factory({
            {ShaderVertexAttributeId::Position0, 0u, 0u, 16u, PixelFormat::R32G32B32A32Float, position.value()},
            {ShaderVertexAttributeId::Normal0, 1u, 0u, 24u, PixelFormat::R32G32B32A32Float, surface.value()},
            {ShaderVertexAttributeId::TexCoord0, 1u, 16u, 24u, PixelFormat::R32G32Float, surface.value()}});
        RHIGraphicsPipelineDesc desc;
        std::vector<RHIVertexBufferBinding> bindings;
        auto status = factory.build_vertex_input(program->data().vertex_inputs, desc.vertex_buffers, desc.vertex_attributes, bindings);
        if (!status) return status;
        desc.vertex_shader = shader.value()->vertex_shader;
        desc.pixel_shader = shader.value()->pixel_shader;
        desc.binding_layout = shader.value()->binding_layout;
        desc.color_attachment_count = shadow ? 0u : 1u;
        if (!shadow) desc.color_formats[0] = PixelFormat::R32UInt;
        desc.depth_stencil_format = PixelFormat::D32Float;
        desc.sample_count = 1u;
        auto state = program->data().graphics_pass_state;
        auto configured = build_shader_graphics_pipeline_desc(desc, state);
        if (!configured) return configured.status();
        auto pipeline = device_->create_graphics_pipeline(configured.value());
        if (!pipeline) return pipeline.status();
        if (shadow)
        {
            state.cull_mode = shader::ShaderGraphicsPassState::CullMode::None;
            configured = build_shader_graphics_pipeline_desc(desc, state);
            if (!configured) return configured.status();
            pipeline = device_->create_graphics_pipeline(configured.value());
            if (!pipeline) return pipeline.status();
        }
        return RHIStatus::success();
    }

    void Renderer::prepare_builtin_shaders(BuiltinShaderUpdateRef request)
    {
        enqueue_render_command("PrepareBuiltinShaders", [this, request = std::move(request)]() noexcept
        {
            if (!request) return;
            try
            {
                request->status = [&]() -> RHIStatus
                {
                    if (!device_ || !shader_program_cache_ || lifecycle_state_.load() != RendererLifecycleState::Running)
                        return RHIStatus::failure(RHIErrorCode::NotReady, "Shader update requires a running Renderer.");
                    if (builtin_update_) return RHIStatus::failure(RHIErrorCode::NotReady, "Another builtin Shader candidate is pending.");
                    if (request->programs.size() == 1u && request->programs.front() &&
                        request->programs.front()->data().shader_name == "Toy3d/ShadowDepth/Default" &&
                        request->programs.front()->data().pass_name == "ShadowDepth")
                    {
                        const auto status = validate_mesh_shader(request->programs.front(), true);
                        if (!status) return status;
                        pending_shadow_shader_ = request->programs.front();
                    }
                    else
                    {
                        auto shaders = global_shader_map_input_->replace(request->programs);
                        if (!shaders.succeeded()) return RHIStatus::failure(RHIErrorCode::InvalidArgument, shaders.error);
                        const auto hit = shaders.shader_map->find(hit_proxy_global_shader_type());
                        if (!hit.succeeded()) return RHIStatus::failure(RHIErrorCode::InvalidArgument, hit.error);
                        auto status = validate_mesh_shader(hit.program, false);
                        if (!status) return status;
                        auto tonemap = std::make_unique<TonemapPassResources>();
                        status = tonemap->initialize(*device_, *shader_program_cache_, *shaders.shader_map);
                        if (!status) return status;
                        if (imgui_renderer_)
                        {
                            status = imgui_renderer_->prepare_shader(*device_, *shader_program_cache_, *shaders.shader_map);
                            if (!status) return status;
                        }
                        pending_tonemap_resources_ = std::move(tonemap);
                        pending_global_shaders_ = std::move(shaders.shader_map);
                    }
                    builtin_update_ = request;
                    return RHIStatus::success();
                }();
            }
            catch (const std::exception& error)
            { request->status = RHIStatus::failure(RHIErrorCode::BackendFailure, error.what()); }
            if (!request->status)
            {
                request->resolved.store(true, std::memory_order_release);
            }
            request->prepared.store(true, std::memory_order_release);
        });
    }

    void Renderer::resolve_builtin_shaders()
    {
        if (!builtin_update_) return;
        const auto decision = builtin_update_->decision.load(std::memory_order_acquire);
        if (decision == BuiltinShaderDecision::Pending) return;
        if (decision == BuiltinShaderDecision::Commit && lifecycle_state_.load() == RendererLifecycleState::Running)
        {
            if (pending_global_shaders_)
            {
                // Only owned strong refs change between frames. Backend commands
                // keep all resources of previous submissions alive until completion.
                global_shader_map_input_ = std::move(pending_global_shaders_);
                tonemap_pass_resources_ = std::move(pending_tonemap_resources_);
                if (imgui_renderer_) imgui_renderer_->publish_shader();
            }
            if (pending_shadow_shader_) mesh_pass_programs_input_.shadow_depth_default = std::move(pending_shadow_shader_);
            builtin_update_->applied = true;
        }
        if (imgui_renderer_) imgui_renderer_->discard_shader();
        pending_global_shaders_.reset(); pending_tonemap_resources_.reset(); pending_shadow_shader_.reset();
        builtin_update_->resolved.store(true, std::memory_order_release);
        builtin_update_.reset();
    }
}
