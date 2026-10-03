#pragma once

#include "assets/animation/animation_preview_asset.h"
#include "scene/editor_actor_state.h"
#include "scene/placement/asset_placement.h"
#include "threading/task_graph/task_graph_interface.h"

namespace toy3d
{
    class EditorWorkspace;
    class EditorCommandHistory;
    class World;

    // One editor-owned candidate job. GT delivery resolves identities again; shutdown joins the worker.
    class MeshAssetBindings final
    {
      public:
        MeshAssetBindings(EditorWorkspace& workspace, EditorCommandHistory& history);
        ~MeshAssetBindings();
        void initialize(TaskGraphInterface& tasks);
        bool request(World& world, std::uint32_t actor, std::uint32_t component, const std::string& role,
                     const AssetId& asset);
        bool set_builtin(World& world, std::uint32_t actor, std::uint32_t component, const std::string& builtin);
        bool place(World& world, const AssetPlacementRequest& request);
        std::uint32_t take_placed_actor();
        void tick(World& world);
        void shutdown();
        bool busy() const;
        const std::string& error() const;

      private:
        struct Result;
        EditorWorkspace& workspace_;
        EditorCommandHistory& history_;
        TaskGraphInterface* tasks_ = nullptr;
        GraphEventRef task_;
        std::shared_ptr<Result> result_;
        World* world_ = nullptr;
        std::uint64_t generation_ = 0;
        std::uint32_t actor_id_ = 0;
        std::uint32_t placed_actor_ = 0;
        EditorActorState before_;
        std::string error_;
    };
} // namespace toy3d
