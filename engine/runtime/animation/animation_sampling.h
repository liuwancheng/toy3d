#pragma once

#include "animation_pose.h"

namespace toy3d::animation_detail
{
    // Internal fast path for immutable snapshots validated by AnimationSequence construction.
    // Publication validates every sample once; each frame visits only the two sampled keys.
    AssetResult<std::vector<Transform>> sample_validated_pose(const SkeletonAssetData& skeleton,
                                                              const AnimationSequenceAsset& sequence, double time);
} // namespace toy3d::animation_detail
