#pragma once

#include "animation/animation_instance.h"
#include "asset/mesh/skeletal_mesh_asset.h"

namespace toy3d
{
    // C++17 inline constants share the conservative bounds contract with callers.
    inline constexpr float animated_bounds_padding = 0.01f;
    inline constexpr float animated_bounds_relative_padding = 0.00001f;

    struct SkeletalMeshDeformationData
    {
        std::shared_ptr<const AnimationBoneLayout> bone_layout;
        std::uint64_t pose_revision = 0;
        std::vector<Matrix4> skin_matrices;
        std::vector<Matrix4> normal_matrices;
        Vector3 bounds_minimum;
        Vector3 bounds_maximum;
        bool has_mesh_bounds = false;
    };

    // Per-consumer immutable mesh binding; failed replacement preserves the previous binding.
    class SkeletalMeshDeformer
    {
      public:
        AssetStatus set_mesh(std::shared_ptr<const AnimationBoneLayout> bone_layout, const SkeletalMeshAsset& mesh);
        AssetResult<SkeletalMeshDeformationData> evaluate(const AnimationEvaluation& evaluation) const;

      private:
        std::shared_ptr<const AnimationBoneLayout> bone_layout_;
        std::shared_ptr<const SkeletalMeshAsset> mesh_;
    };

    // Three affine rows followed by three normal rows per section-local bone.
    AssetResult<std::vector<Vector4>> build_bone_matrix_rows(const SkeletalMeshDeformationData& data,
                                                             const std::vector<std::uint32_t>& bone_map);
} // namespace toy3d
