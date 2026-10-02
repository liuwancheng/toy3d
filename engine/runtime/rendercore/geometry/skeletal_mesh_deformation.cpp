#include "skeletal_mesh_deformation.h"

#include <algorithm>
#include <cmath>

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }

        void extend(SkeletalMeshDeformationData& pose, const Vector3& point)
        {
            if (!pose.has_mesh_bounds)
            {
                pose.bounds_minimum = point;
                pose.bounds_maximum = point;
                pose.has_mesh_bounds = true;
            }
            else
            {
                pose.bounds_minimum =
                    Vector3(std::min(pose.bounds_minimum.x, point.x), std::min(pose.bounds_minimum.y, point.y),
                            std::min(pose.bounds_minimum.z, point.z));
                pose.bounds_maximum =
                    Vector3(std::max(pose.bounds_maximum.x, point.x), std::max(pose.bounds_maximum.y, point.y),
                            std::max(pose.bounds_maximum.z, point.z));
            }
        }
    } // namespace

    // --------------------------------------------------------------------------
    // SkeletalMeshDeformer: validated mesh binding and final pose-derived GPU data
    // --------------------------------------------------------------------------
    AssetStatus SkeletalMeshDeformer::set_mesh(std::shared_ptr<const AnimationBoneLayout> bone_layout,
                                               const SkeletalMeshAsset& mesh)
    {
        if (!bone_layout || !bone_layout->status().succeeded())
        {
            return invalid("mesh deformation requires a valid bone layout");
        }
        const auto valid =
            validate_skeletal_mesh_compatibility(mesh, bone_layout->skeleton_id(), bone_layout->skeleton());
        if (!valid.succeeded())
        {
            return valid;
        }
        auto candidate = std::make_shared<const SkeletalMeshAsset>(mesh);
        bone_layout_ = std::move(bone_layout);
        mesh_ = std::move(candidate);
        return AssetStatus::success();
    }

    AssetResult<SkeletalMeshDeformationData> SkeletalMeshDeformer::evaluate(const AnimationEvaluation& evaluation) const
    {
        const auto& component_pose = evaluation.component_pose;
        if (!mesh_ || !component_pose.bone_layout || !bone_layout_->compatible(*component_pose.bone_layout) ||
            component_pose.bone_matrices.size() != bone_layout_->skeleton().bones.size())
        {
            return AssetResult<SkeletalMeshDeformationData>(
                invalid("mesh deformation has no compatible component pose"));
        }
        SkeletalMeshDeformationData pose;
        pose.bone_layout = component_pose.bone_layout;
        pose.pose_revision = evaluation.revision;
        pose.skin_matrices.resize(component_pose.bone_matrices.size());
        pose.normal_matrices.resize(component_pose.bone_matrices.size());
        for (std::size_t i = 0; i < component_pose.bone_matrices.size(); ++i)
        {
            const auto& component = component_pose.bone_matrices[i];
            const auto skin = component * mesh_->geometry.inverse_bind_matrices[i];
            Matrix4 inverse;
            if (!is_finite(skin) || !try_inverse(skin, inverse))
            {
                return AssetResult<SkeletalMeshDeformationData>(invalid("singular or nonfinite skin matrix"));
            }
            pose.skin_matrices[i] = skin;
            pose.normal_matrices[i] = transpose(inverse);
            const auto& bounds = mesh_->geometry.bone_local_bounds[i];
            if (bounds.influenced)
            {
                for (std::uint32_t corner = 0; corner < 8; ++corner)
                {
                    const Vector3 point((corner & 1) ? bounds.maximum.x : bounds.minimum.x,
                                        (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
                                        (corner & 4) ? bounds.maximum.z : bounds.minimum.z);
                    const auto transformed = transform_position(component, point);
                    if (!is_finite(transformed))
                    {
                        return AssetResult<SkeletalMeshDeformationData>(invalid("nonfinite animated bounds"));
                    }
                    extend(pose, transformed);
                }
            }
        }
        if (pose.has_mesh_bounds)
        {
            const float magnitude = std::max({std::abs(pose.bounds_minimum.x), std::abs(pose.bounds_minimum.y),
                                              std::abs(pose.bounds_minimum.z), std::abs(pose.bounds_maximum.x),
                                              std::abs(pose.bounds_maximum.y), std::abs(pose.bounds_maximum.z)});
            const float distance = animated_bounds_padding + magnitude * animated_bounds_relative_padding;
            const Vector3 padding(distance, distance, distance);
            pose.bounds_minimum = pose.bounds_minimum - padding;
            pose.bounds_maximum = pose.bounds_maximum + padding;
            if (!is_finite(pose.bounds_minimum) || !is_finite(pose.bounds_maximum))
            {
                return AssetResult<SkeletalMeshDeformationData>(invalid("animated bounds padding overflow"));
            }
        }
        return AssetResult<SkeletalMeshDeformationData>(std::move(pose));
    }

    AssetResult<std::vector<Vector4>> build_bone_matrix_rows(const SkeletalMeshDeformationData& pose,
                                                             const std::vector<std::uint32_t>& bone_map)
    {
        if (bone_map.empty() || bone_map.size() > max_section_bones ||
            pose.skin_matrices.size() != pose.normal_matrices.size())
        {
            return AssetResult<std::vector<Vector4>>(invalid("invalid section matrix input"));
        }
        std::vector<Vector4> rows;
        rows.reserve(bone_map.size() * 6);
        for (const auto bone : bone_map)
        {
            if (bone >= pose.skin_matrices.size() || !is_finite(pose.skin_matrices[bone]) ||
                !is_finite(pose.normal_matrices[bone]))
            {
                return AssetResult<std::vector<Vector4>>(invalid("out of range or nonfinite bone matrix"));
            }
            for (std::size_t row = 0; row < 3; ++row)
            {
                const auto& matrix = pose.skin_matrices[bone];
                rows.emplace_back(matrix.at(0, row), matrix.at(1, row), matrix.at(2, row), matrix.at(3, row));
            }
            for (std::size_t row = 0; row < 3; ++row)
            {
                const auto& matrix = pose.normal_matrices[bone];
                rows.emplace_back(matrix.at(0, row), matrix.at(1, row), matrix.at(2, row), 0.0f);
            }
        }
        return AssetResult<std::vector<Vector4>>(std::move(rows));
    }
} // namespace toy3d
