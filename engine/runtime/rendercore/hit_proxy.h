#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace toy3d
{
    // A Game Thread click snapshot. Coordinates refer to the scene image in
    // physical pixels, never to the containing ImGui window.
    struct HitProxyRequest
    {
        std::uint64_t request_id = 0;
        std::uint64_t viewport_generation = 0;
        std::uint64_t scene_generation = 0;
        std::uint32_t pixel_x = 0;
        std::uint32_t pixel_y = 0;
    };

    // The pixel value is scoped to one submitted HitProxy pass, not to any
    // GameScene object lifetime. Zero is reserved for the cleared background.
    struct HitProxyId
    {
        std::uint32_t value = 0;
    };

    enum class HitProxyTargetKind
    {
        None,
        Actor,
        Component,
        MeshSection
    };

    struct HitProxyTarget
    {
        HitProxyTargetKind kind = HitProxyTargetKind::None;
        std::uint32_t actor_id = 0;
        // Component IDs are unique within their World.
        std::uint32_t component_id = 0;
        std::uint32_t mesh_section_index = 0;
    };

    // The table travels with its readback so an older GPU result cannot be
    // interpreted against a newer frame's draw list.
    using HitProxyTable = std::vector<HitProxyTarget>;

    inline bool resolve_hit_proxy(HitProxyId id, const HitProxyTable& table, HitProxyTarget& target)
    {
        target = {};
        if (id.value == 0u)
        {
            return true;
        }
        if (static_cast<std::size_t>(id.value) > table.size())
        {
            return false;
        }
        target = table[id.value - 1u];
        if (target.kind == HitProxyTargetKind::None || target.actor_id == 0u)
        {
            return false;
        }
        return target.kind == HitProxyTargetKind::Actor || target.component_id != 0u;
    }

    struct HitProxyResult
    {
        HitProxyRequest request;
        HitProxyId id;
        HitProxyTarget target;
    };
} // namespace toy3d
