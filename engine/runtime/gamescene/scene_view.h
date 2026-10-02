#pragma once

#include "rendercore/view/scene_view.h"

#include <vector>

namespace toy3d
{
    class World;
    // Shared Game/PIE camera policy: first Camera component, otherwise a stable
    // observation pose. The editor's authored observation camera is independent.
    void build_game_scene_views(const World& world, std::vector<SceneView>& views, const Extent& extent);
} // namespace toy3d
