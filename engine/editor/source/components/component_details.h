#pragma once

#include "components/component_editor_registry.h"

namespace toy3d
{
    void finish_component_edit(ComponentDetailsContext& context, const SceneComponentData& candidate, bool changed);
    bool capture_component_edit(ComponentDetailsContext& context, SceneComponentData& data);
    void draw_component_transform(ComponentDetailsContext& context);
}
