#pragma once

#include "assets/preview/asset_preview_scene.h"

namespace toy3d
{
    class EditorWorkspace;

    // Edits only the caller's preview settings; scene publication remains with its owner.
    bool draw_preview_scene_settings(const EditorWorkspace& workspace, PreviewSceneSettings& settings);
} // namespace toy3d
