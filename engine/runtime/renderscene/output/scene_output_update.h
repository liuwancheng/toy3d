#pragma once

#include "renderscene/output/scene_output.h"
#include "renderscene/output/scene_output_revision.h"

namespace toy3d
{
    enum class SceneOutputUpdateOperation
    {
        Update,
        Release
    };

    struct SceneOutputUpdate
    {
        SceneOutputUpdateOperation operation =
            SceneOutputUpdateOperation::Update;
        SceneOutputId output_id;
        SceneOutputRevision revision;
        SceneOutputExtent extent;
    };
}
