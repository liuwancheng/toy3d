#pragma once

#include "drivers/rhi/rhi_device.h"
#include "rendercore/hit_proxy.h"
#include "rendercore/material/material_shader_map_validation.h"
#include "rendercore/shader/builtin_shader_update.h"
#include "renderscene/builtin_mesh_pass_programs.h"
#include "threading/threading_types.h"
#include "ui/imgui_draw_data.h"
#include "ui/ui_texture_work.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace toy3d
{
    class VertexFactory;
    class RenderResourceManager;
    class RenderScene;
    class SceneRenderTargets;
    class SceneRenderer;
    class SceneInterface;
    struct SceneRenderFeedback;
    class GlobalShaderMap;
    class RHIShaderProgramCache;
    class TaskGraphInterface;
    class TonemapPassResources;
    class EnvironmentBackgroundPassResources;
    class ImGuiRenderer;
    class ViewportOutputTarget;
    class UiTextureRegistry;

    struct ViewportFrameOutput
    {
        bool sample_in_ui = false;
        Extent window_extent;
        Extent scene_extent;
        ImGuiTextureId texture_id;
        HitProxyRequest hit_proxy_request;
        bool play_scene = false;
        std::shared_ptr<SceneRenderFeedback> scene_feedback;
    };

    enum class RendererLifecycleState
    {
        Stopped,
        Starting,
        Running,
        Stopping,
        Terminal
    };

    // Immutable cross-thread snapshot. It carries diagnostics only and grants no
    // access to Renderer-owned services or mutable Render-side state.
    struct RendererStatus
    {
      public:
        RendererLifecycleState lifecycle_state() const noexcept;
        bool has_error() const noexcept;
        RHIErrorCode error_code() const noexcept;
        const std::string& error_message() const noexcept;
        const std::string& secondary_diagnostic() const noexcept;

      private:
        friend class Renderer;

        RendererLifecycleState lifecycle_state_ = RendererLifecycleState::Stopped;
        RHIErrorCode error_code_ = RHIErrorCode::None;
        std::string error_message_;
        std::string secondary_diagnostic_;
    };

    // Engine-owned stable shell. Its mutable Render-side domain is initialized and
    // torn down only on the logical Rendering Thread.
    class Renderer final
    {
      public:
        Renderer(TaskGraphInterface& task_graph, RHISurfaceRef primary_surface, RHIViewportContextDesc viewport_desc,
                 std::function<RHIResult<std::unique_ptr<RHIDevice>>()> device_factory,
                 std::shared_ptr<const GlobalShaderMap> global_shader_map,
                 std::unique_ptr<ImGuiFontAtlasData> imgui_font_atlas = nullptr, bool enable_preview_scene = false,
                 BuiltinMeshPassPrograms mesh_pass_programs = {}, bool enable_play_scene = false);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) = delete;
        Renderer& operator=(Renderer&&) = delete;

        ThreadStatus initialize();
        ThreadStatus teardown();
        void draw_frame(std::unique_ptr<SceneRenderer> scene_renderer,
                        std::unique_ptr<ImGuiDrawData> ui_draw_data = nullptr, ViewportFrameOutput output = {},
                        UiRenderWork work = {}, std::unique_ptr<SceneRenderer> preview_renderer = nullptr);
        RendererStatus status() const;
        bool poll_hit_proxy(HitProxyResult& result);
        // Published only between successful logical-RT initialize and teardown.
        // The pointer is non-owning and exposes no concrete RenderScene state to GT.
        SceneInterface* scene_interface() const;
        SceneInterface* preview_scene_interface() const;
        SceneInterface* play_scene_interface() const;
        bool poll_ui_texture(UiTextureResult& result);
        void validate_material_shader_map(MaterialShaderMapValidationRef request);
        void prepare_builtin_shaders(BuiltinShaderUpdateRef request);

      private:
        bool is_on_logical_rendering_thread() const;
        ThreadStatus fail_startup(const RHIStatus& failure);
        RHIStatus ensure_primary_frame_extent(const Extent& extent);
        RHIResult<RHIFrameEndResult> render_frame(SceneRenderer* scene_renderer, const ImGuiDrawData* ui_draw_data,
                                                  const ViewportFrameOutput& output);
        void enter_terminal(const RHIStatus& failure) noexcept;
        void append_secondary_diagnostic(const RHIStatus& failure) noexcept;
        void release_domain(bool terminal) noexcept;
        void collect_hit_proxy_readbacks();
        void collect_ui_readbacks();
        void resolve_builtin_shaders();
        RHIStatus validate_mesh_shader(const ShaderMapProgramRef& program, const VertexFactory* geometry = nullptr);
        RHIStatus record_ui_work(RHIGraphicsCommandContext& context, RHIReadbackRef& capture);

        struct PendingHitReadback
        {
            HitProxyRequest request;
            RHIReadbackRef readback;
            HitProxyTable table;
        };

        TaskGraphInterface& task_graph_;
        RHISurfaceRef primary_surface_input_;
        RHIViewportContextDesc viewport_desc_;
        std::function<RHIResult<std::unique_ptr<RHIDevice>>()> device_factory_;
        std::shared_ptr<const GlobalShaderMap> global_shader_map_input_;
        BuiltinMeshPassPrograms mesh_pass_programs_input_;
        std::unique_ptr<ImGuiFontAtlasData> imgui_font_atlas_input_;

        std::unique_ptr<RHIDevice> device_;
        std::unique_ptr<RHIShaderProgramCache> shader_program_cache_;
        std::unique_ptr<RenderResourceManager> resource_manager_;
        std::unique_ptr<RenderScene> render_scene_;
        bool enable_play_scene_ = false;
        std::unique_ptr<RenderScene> play_scene_;
        std::atomic<SceneInterface*> published_play_interface_{nullptr};
        bool enable_preview_scene_ = false;
        std::unique_ptr<RenderScene> preview_scene_;
        std::unique_ptr<SceneRenderTargets> preview_targets_;
        std::unique_ptr<UiTextureRegistry> ui_textures_;
        UiRenderWork pending_ui_work_;
        std::unique_ptr<SceneRenderer> pending_preview_renderer_;
        struct PendingUiReadback
        {
            std::uint64_t request_id = 0;
            ImGuiTextureId texture_id;
            Extent extent;
            RHIReadbackRef readback;
        };
        std::deque<PendingUiReadback> pending_ui_readbacks_;
        std::uint32_t pending_preview_attempts_ = 0;
        std::deque<UiTextureResult> ui_results_;
        mutable std::mutex ui_results_mutex_;
        std::atomic<SceneInterface*> published_preview_interface_{nullptr};
        std::unique_ptr<SceneRenderTargets> scene_render_targets_;
        std::unique_ptr<ViewportOutputTarget> viewport_output_target_;
        std::unique_ptr<TonemapPassResources> tonemap_pass_resources_;
        std::unique_ptr<EnvironmentBackgroundPassResources> environment_background_resources_;
        std::unique_ptr<ImGuiRenderer> imgui_renderer_;
        BuiltinShaderUpdateRef builtin_update_;
        std::shared_ptr<const GlobalShaderMap> pending_global_shaders_;
        std::unique_ptr<TonemapPassResources> pending_tonemap_resources_;
        std::unique_ptr<EnvironmentBackgroundPassResources> pending_environment_background_resources_;
        BuiltinMeshPassPrograms pending_mesh_pass_programs_;
        std::unique_ptr<RHIViewportContext> primary_viewport_;
        RHITextureRef placeholder_texture_;
        RHITextureViewRef placeholder_texture_view_;
        RHISamplerRef placeholder_sampler_;

        std::atomic<RendererLifecycleState> lifecycle_state_{RendererLifecycleState::Stopped};
        std::atomic<SceneInterface*> published_scene_interface_{nullptr};
        mutable std::mutex status_mutex_;
        RHIErrorCode first_error_code_ = RHIErrorCode::None;
        std::string first_error_message_;
        std::string secondary_diagnostic_;
        std::deque<PendingHitReadback> pending_hit_readbacks_;
        std::deque<HitProxyResult> completed_hit_results_;
        mutable std::mutex hit_results_mutex_;
    };
} // namespace toy3d
