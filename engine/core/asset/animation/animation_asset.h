#pragma once

#include "asset/animation/animation_asset_data.h"
#include "asset/asset_pair.h"
#include "animation_asset_reflection.h"

namespace toy3d
{
    // C++17 inline constants give tools and runtime the same bounded format contract.
    inline constexpr std::size_t max_skeleton_bones = 1024;
    inline constexpr double max_animation_duration = 600.0;
    inline constexpr std::size_t max_animation_samples = 1000000;
    inline constexpr float skeleton_reference_tolerance = 0.0001f;

    struct AnimationTrack
    {
        std::uint32_t bone_index = 0;
        std::vector<Transform> samples;
    };

    struct AnimationSequenceAsset
    {
        AnimationSequenceAssetData data;
        std::vector<AnimationTrack> tracks;
    };

    AssetStatus validate_animation_transform(const Transform& transform);
    AssetStatus validate_skeleton(const SkeletonAssetData& skeleton);
    AssetStatus validate_skeleton_compatibility(const SkeletonAssetData& expected, const SkeletonAssetData& source);
    AssetResult<std::string> skeleton_reference_hash(const SkeletonAssetData& skeleton);
    AssetStatus validate_skeleton_reference(const AssetRef& reference, const std::string& hash);
    std::uint32_t animation_sample_count(double duration, std::uint32_t sample_rate);
    AssetStatus validate_animation_sequence(const AnimationSequenceAsset& sequence);
    AssetStatus validate_animation_compatibility(const AnimationSequenceAsset& sequence, const AssetId& skeleton_id,
                                                 const SkeletonAssetData& skeleton);
    AssetResult<std::vector<std::uint8_t>> encode_animation_tracks(const AnimationSequenceAsset& sequence);
    AssetResult<AnimationSequenceAsset> decode_animation_tracks(const AnimationSequenceAssetData& data,
                                                                const std::vector<std::uint8_t>& bytes);
    AssetResult<AssetPairBytes> encode_skeleton_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                           const SkeletonAssetData& skeleton);
    AssetResult<AssetPairBytes> encode_animation_sequence_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                                     const AnimationSequenceAsset& sequence);
    AssetResult<SkeletonAssetData> decode_skeleton_asset_pair(const AssetPair& pair);
    AssetResult<AnimationSequenceAsset> decode_animation_sequence_asset_pair(const AssetPair& pair);
} // namespace toy3d
