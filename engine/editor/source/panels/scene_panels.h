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
    class MaterialAssignments;
    class AssetResourcePicker;
    class MeshAssetBindings;

    void draw_world_settings(World& world, const EditorWorkspace& workspace, EditorCommandHistory& history,
                             std::string& error);
    bool draw_outliner(World& world, EditorSelection& selection, EditorCommandHistory& history,
                       const ActorFactory& factory, SceneViewport& viewport);
    void draw_details(World& world, EditorSelection& selection, EditorCommandHistory& history,
                      const EditorWorkspace& workspace, SceneViewport& viewport, MaterialAssignments& materials,
                      std::string& material_error, AssetResourcePicker* picker = nullptr,
                      MeshAssetBindings* bindings = nullptr);
} // namespace toy3d
