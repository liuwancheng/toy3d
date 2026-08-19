#pragma once

#include <cstdint>

namespace toy3d
{
    enum class RenderDirtyFlags : std::uint8_t
    {
        None = 0,
        Transform = 1 << 0,
        State = 1 << 1,
        DynamicData = 1 << 2
    };

    constexpr RenderDirtyFlags operator|(RenderDirtyFlags lhs, RenderDirtyFlags rhs)
    {
        return static_cast<RenderDirtyFlags>(
            static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
    }

    constexpr RenderDirtyFlags& operator|=(RenderDirtyFlags& lhs, RenderDirtyFlags rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    constexpr bool has_render_dirty_flag(
        RenderDirtyFlags flags,
        RenderDirtyFlags required)
    {
        return (static_cast<std::uint8_t>(flags) &
            static_cast<std::uint8_t>(required)) != 0;
    }
}
