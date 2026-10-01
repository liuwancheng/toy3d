#pragma once

#include <cstdint>
#include <vector>

namespace toy3d
{
    class RenderScene;
    class ViewInfo;
    class RHIStatus;
    struct LightSceneData;

    // Computes only current-frame camera visibility and candidate MeshBatches.
    // The operation retains no state between calls.
    void compute_scene_visibility(const RenderScene& render_scene, std::vector<ViewInfo>& view_infos);
    RHIStatus compute_shadow_visibility(const RenderScene& render_scene, const LightSceneData* directional_light,
                                        std::vector<ViewInfo>& view_infos, std::uint32_t shadow_resolution = 0u);
} // namespace toy3d
