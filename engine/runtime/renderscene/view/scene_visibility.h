#pragma once

#include <vector>

namespace toy3d
{
    class RenderScene;
    class ViewInfo;

    // Computes only current-frame camera visibility and candidate MeshBatches.
    // The operation retains no state between calls.
    void compute_scene_visibility(const RenderScene& render_scene, std::vector<ViewInfo>& view_infos);
} // namespace toy3d
