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
    class ViewportOutputTarget;
    class GlobalShaderMap;
    struct ViewportFrameOutput;

    // Executes one explicit viewport-frame transaction. Renderer owns the
    // supplied scene/UI payloads; a hidden embedded viewport may omit the
    // scene payload while the window UI still submits a frame.
    RHIResult<RHIFrameEndResult> render_viewport_frame(
        SceneRenderer* scene_renderer, const ImGuiDrawData* ui_draw_data, const ViewportFrameOutput& output,
        RenderScene& render_scene, RHIDevice& device,
        RHIShaderProgramCache& shader_program_cache, RenderResourceManager& resource_manager,
        RHIViewportContext& viewport, SceneRenderTargets& scene_render_targets,
        TonemapPassResources& tonemap_pass_resources, ImGuiRenderer* imgui_renderer,
        ViewportOutputTarget& viewport_output_target, const GlobalShaderMap* global_shader_map = nullptr,
        RHIReadbackRef* recorded_readback = nullptr);

} // namespace toy3d
