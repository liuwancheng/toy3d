#pragma once

#include "scene/editor_command_history.h"
#include "asset/scene/scene_asset.h"
#include <map>
#include <string>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;
    class EditorSelection;
    class SceneViewport;

    // Owns the author session, not the World or runtime objects.
    class EditorSceneSession
    {
      public:
        EditorSceneSession(EditorWorkspace& workspace, ActorFactory& factory, MaterialAssignments& materials,
                           EditorSelection& selection, SceneViewport& viewport);
        void bind(World& world);
        EditorCommandHistory& history()
        {
            return history_;
        }
        const EditorCommandHistory& history() const
        {
            return history_;
        }
        const AssetId& asset_id() const
        {
            return asset_id_;
        }
        const VirtualPath& path() const
        {
            return path_;
        }
        const std::string& error() const
        {
            return error_;
        }
        bool dirty() const;
        bool new_scene();
        bool open(const AssetId& id);
        bool open_path(const std::string& path);
        bool save(const VirtualPath& path, bool create_new);
        bool capture(SceneAssetData& data);
        bool replace(const SceneAssetData& data);

      private:
        void clear_interaction();
        void remap(std::uint32_t old_id, std::uint32_t new_id,
                   const std::map<std::uint32_t, std::uint32_t>& components);
        EditorWorkspace& workspace_;
        ActorFactory& factory_;
        MaterialAssignments& materials_;
        EditorSelection& selection_;
        SceneViewport& viewport_;
        EditorCommandHistory history_;
        World* world_ = nullptr;
        AssetId asset_id_;
        VirtualPath path_;
        std::map<std::uint32_t, std::string> actor_ids_;
        std::map<std::uint32_t, std::string> component_ids_;
        std::vector<std::uint8_t> published_bytes_;
        std::string error_;
    };
} // namespace toy3d
