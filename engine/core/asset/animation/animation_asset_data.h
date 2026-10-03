#pragma once

#include "asset/asset_identity.h"
#include "math/transform.h"
#include "reflection/reflection_macros.h"

#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.SkeletonBone", 1)
    struct SkeletonBone
    {
        TOY3D_PROPERTY("name", Visible)
        std::string name;
        TOY3D_PROPERTY("parent_index", Visible)
        std::int32_t parent_index = -1;
        TOY3D_PROPERTY("reference_local_transform", Visible)
        Transform reference_local_transform;
    };

    TOY3D_REFLECT_TYPE("toy3d.SkeletonAssetData", 1)
    struct SkeletonAssetData
    {
        TOY3D_PROPERTY("bones", Visible)
        std::vector<SkeletonBone> bones;
    };

    TOY3D_REFLECT_TYPE("toy3d.AnimationSequenceAssetData", 1)
    struct AnimationSequenceAssetData
    {
        TOY3D_PROPERTY("skeleton", Visible)
        AssetRef skeleton;
        TOY3D_PROPERTY("skeleton_reference_hash", Visible)
        std::string skeleton_reference_hash;
        TOY3D_PROPERTY("duration", Visible)
        double duration = 0.0;
        TOY3D_PROPERTY("sample_rate", Visible)
        std::uint32_t sample_rate = 30;
        TOY3D_PROPERTY("sample_count", Visible)
        std::uint32_t sample_count = 1;
        TOY3D_PROPERTY("track_count", Visible)
        std::uint32_t track_count = 0;
        TOY3D_PROPERTY("tracks_segment", Visible)
        std::string tracks_segment = "animation_tracks";
    };

    TOY3D_REFLECT_TYPE("toy3d.SkeletalMeshAssetData", 2)
    struct SkeletalMeshAssetData
    {
        TOY3D_PROPERTY("skeleton", Visible)
        AssetRef skeleton;
        TOY3D_PROPERTY("skeleton_reference_hash", Visible)
        std::string skeleton_reference_hash;
        TOY3D_PROPERTY("material_slots", Visible)
        std::vector<std::string> material_slots;
        TOY3D_PROPERTY("vertex_count", Visible)
        std::uint32_t vertex_count = 0;
        TOY3D_PROPERTY("index_count", Visible)
        std::uint32_t index_count = 0;
        TOY3D_PROPERTY("geometry_segment", Visible)
        std::string geometry_segment = "skeletal_geometry";
    };
} // namespace toy3d
