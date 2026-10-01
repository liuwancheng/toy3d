#include "scene/editor_selection.h"

#include "gamescene/world/world.h"
#include "asset/asset_index.h"

namespace toy3d
{
    void EditorSelection::select_actor(World& world, std::uint32_t actor_id)
    {
        world_ = &world;
        actor_id_ = world.find_actor_by_id(actor_id) != nullptr ? actor_id : 0u;
        focus_ = actor_id_ != 0u ? EditorSelectionFocus::Actor : EditorSelectionFocus::None;
    }

    void EditorSelection::clear_actor()
    {
        world_ = nullptr;
        actor_id_ = 0u;
        if (focus_ == EditorSelectionFocus::Actor)
            focus_ = asset_id_.valid() ? EditorSelectionFocus::Asset : EditorSelectionFocus::None;
    }

    Actor* EditorSelection::resolve_actor(World& world)
    {
        if (world_ != &world || actor_id_ == 0u)
        {
            clear_actor();
            return nullptr;
        }
        Actor* const actor = world.find_actor_by_id(actor_id_);
        if (actor == nullptr)
            clear_actor();
        return actor;
    }

    void EditorSelection::select_asset(const AssetId& asset_id)
    {
        asset_id_ = asset_id;
        focus_ = asset_id_.valid() ? EditorSelectionFocus::Asset : EditorSelectionFocus::None;
    }

    void EditorSelection::clear_asset()
    {
        asset_id_ = {};
        if (focus_ == EditorSelectionFocus::Asset)
            focus_ = actor_id_ != 0u ? EditorSelectionFocus::Actor : EditorSelectionFocus::None;
    }

    const AssetLocation* EditorSelection::resolve_asset(const AssetIndex& index)
    {
        if (!asset_id_.valid()) return nullptr;
        const AssetLocation* const location = index.find(asset_id_);
        if (location == nullptr) clear_asset();
        return location;
    }
} // namespace toy3d
