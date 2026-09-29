#pragma once

#include <string>

namespace toy3d
{
    class EditorSelection;
    class EditorCommandHistory;
    class EditorWorkspace;
    class World;
    class ActorFactory;
    class SceneViewport;

    bool draw_outliner(World& world, EditorSelection& selection, EditorCommandHistory& history, const ActorFactory& factory);
    void draw_details(World& world, EditorSelection& selection, EditorCommandHistory& history,
                      const EditorWorkspace& workspace, SceneViewport& viewport);
} // namespace toy3d
