#pragma once

#include "asset/scene/scene_asset_data.h"
#include "gamescene/component/scene_component.h"

namespace toy3d
{
    SceneComponent* create_scene_component(Actor& actor, const std::string& type);
    bool capture_scene_component(const SceneComponent& component, SceneComponentData& data);
    bool apply_scene_component(SceneComponent& component, const SceneComponentData& data);
}
