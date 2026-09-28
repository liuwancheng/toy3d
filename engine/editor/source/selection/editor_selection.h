#pragma once

#include "asset_identity.h"

#include <cstdint>

namespace toy3d
{
    class Actor;
    class AssetIndex;
    struct AssetLocation;
    class World;

    enum class EditorSelectionFocus
    {
        None,
        Actor,
        Asset
    };

    // Actor IDs have meaning only within the World that issued them.
    class EditorSelection
    {
      public:
        void select_actor(World& world, std::uint32_t actor_id);
        void clear_actor();
        Actor* resolve_actor(World& world);
        std::uint32_t actor_id() const { return actor_id_; }
        void select_asset(const AssetId& asset_id);
        void clear_asset();
        const AssetLocation* resolve_asset(const AssetIndex& index);
        const AssetId& asset_id() const { return asset_id_; }
        EditorSelectionFocus focus() const { return focus_; }

      private:
        World* world_ = nullptr;
        std::uint32_t actor_id_ = 0u;
        AssetId asset_id_;
        EditorSelectionFocus focus_ = EditorSelectionFocus::None;
    };
} // namespace toy3d
