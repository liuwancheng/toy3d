#pragma once

#include "reflection/reflection_macros.h"

#include <string>
#include <vector>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.EventData", 1)
    struct EventData
    {
        TOY3D_PROPERTY("id")
        std::string id;

        TOY3D_PROPERTY("time", Edit)
        float time = 0.0f;

        TOY3D_PROPERTY("label", Edit)
        std::string label;
    };

    TOY3D_REFLECT_TYPE("toy3d.TimelineAsset", 1)
    struct TimelineAsset
    {
        TOY3D_PROPERTY("events", Edit)
        std::vector<EventData> events;
    };
} // namespace toy3d
