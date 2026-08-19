#pragma once

#include "rendercore/render_id.h"

#include <cstdint>

namespace toy3d
{
    enum class SceneOutputType
    {
        Present,
        Offscreen
    };

    struct SceneOutputExtent
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    inline bool operator==(
        const SceneOutputExtent& lhs,
        const SceneOutputExtent& rhs)
    {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    inline bool operator!=(
        const SceneOutputExtent& lhs,
        const SceneOutputExtent& rhs)
    {
        return !(lhs == rhs);
    }

    struct SceneOutput
    {
        SceneOutputId output_id;
        SceneOutputType type = SceneOutputType::Present;
        SceneOutputExtent extent;
    };
}
