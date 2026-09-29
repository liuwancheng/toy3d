#pragma once

#include <string>

#include "asset_identity.h"
#include "math/transform.h"

namespace toy3d
{
    class World;
    class EditorWorkspace;
    class ActorFactory;
    class EditorCommandHistory;

    constexpr const char* ASSET_DRAG_PAYLOAD = "TOY3D_ASSET";

    struct AssetPlacementRequest
    {
        AssetId asset_id;
        Transform transform;
        bool on_ground = false;
    };

    // Resolve identity at delivery, then publish through the existing command
    // history. Failure leaves the World and its history unchanged.
    std::uint32_t place_static_mesh_asset(EditorWorkspace& workspace, World& world,
        ActorFactory& factory, EditorCommandHistory& history, const AssetPlacementRequest& request, std::string& error);
}
