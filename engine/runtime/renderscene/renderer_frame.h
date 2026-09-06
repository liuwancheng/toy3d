#pragma once

#include "drivers/rhi/rhi_viewport_context.h"

namespace toy3d
{
    struct ImGuiDrawData;
    class ImGuiRenderer;
    class RenderResourceManager;
    class RenderScene;
    class RHIDevice;
    class RHIShaderProgramCache;
    class SceneRenderer;
    class SceneRenderTargets;
    class TonemapPassResources;

    // Executes one explicit viewport-frame transaction. Renderer owns the
    // supplied scene/UI payloads; this function only borrows them while it
    // records scene, final-output and optional UI passes into one submission.
    RHIResult<RHIFrameEndResult> render_viewport_frame(
        SceneRenderer& scene_renderer, const ImGuiDrawData* ui_draw_data, RenderScene& render_scene, RHIDevice& device,
        RHIShaderProgramCache& shader_program_cache, RenderResourceManager& resource_manager,
        RHIViewportContext& viewport, SceneRenderTargets& scene_render_targets,
        TonemapPassResources& tonemap_pass_resources, ImGuiRenderer* imgui_renderer);
} // namespace toy3d
