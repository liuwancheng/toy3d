#include "animation_pose.h"

#include <algorithm>
#include <cmath>

#include "animation_sampling.h"

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }
    } // namespace

    // --------------------------------------------------------------------------
    // AnimationBoneLayout: immutable skeleton identity and ordered reference data
    // --------------------------------------------------------------------------
    AnimationBoneLayout::AnimationBoneLayout(const AssetId& skeleton_id, const SkeletonAssetData& skeleton)
        : skeleton_id_(skeleton_id), skeleton_(skeleton)
    {
        if (!skeleton_id.valid())
        {
            status_ = invalid("invalid animation skeleton identity");
            return;
        }
        const auto hash = skeleton_reference_hash(skeleton_);
        if (!hash.succeeded())
        {
            status_ = hash.status();
            return;
        }
        reference_hash_ = hash.value();
    }

    const AssetStatus& AnimationBoneLayout::status() const
    {
        return status_;
    }

    const AssetId& AnimationBoneLayout::skeleton_id() const
    {
        return skeleton_id_;
    }

    const std::string& AnimationBoneLayout::reference_hash() const
    {
        return reference_hash_;
    }

    const SkeletonAssetData& AnimationBoneLayout::skeleton() const
    {
        return skeleton_;
    }

    bool AnimationBoneLayout::compatible(const AnimationBoneLayout& other) const
    {
        return status_.succeeded() && other.status_.succeeded() && skeleton_id_ == other.skeleton_id_ &&
               reference_hash_ == other.reference_hash_;
    }

    AssetStatus validate_animation_pose(const AnimationPose& pose)
    {
        if (!pose.bone_layout || !pose.bone_layout->status().succeeded() ||
            pose.local_transforms.size() != pose.bone_layout->skeleton().bones.size())
        {
            return invalid("animation pose has no valid matching bone layout");
        }
        for (const auto& transform : pose.local_transforms)
        {
            const auto status = validate_animation_transform(transform);
            if (!status.succeeded())
            {
                return status;
            }
        }
        return AssetStatus::success();
    }

    AssetResult<AnimationPose> make_reference_pose(std::shared_ptr<const AnimationBoneLayout> bone_layout)
    {
        AnimationPose pose;
        pose.bone_layout = std::move(bone_layout);
        const auto status = reset_to_reference_pose(pose);
        if (!status.succeeded())
        {
            return AssetResult<AnimationPose>(status);
        }
        return AssetResult<AnimationPose>(std::move(pose));
    }

    AssetStatus reset_to_reference_pose(AnimationPose& pose)
    {
        if (!pose.bone_layout || !pose.bone_layout->status().succeeded())
        {
            return invalid("reference pose requires a valid bone layout");
        }
        pose.local_transforms.clear();
        for (const auto& bone : pose.bone_layout->skeleton().bones)
        {
            pose.local_transforms.push_back(bone.reference_local_transform);
        }
        return AssetStatus::success();
    }

    AssetResult<AnimationPose> sample_animation_pose(std::shared_ptr<const AnimationBoneLayout> bone_layout,
                                                     const AnimationSequenceAsset& sequence, double time)
    {
        if (!bone_layout || !bone_layout->status().succeeded())
        {
            return AssetResult<AnimationPose>(invalid("sampling requires a valid bone layout"));
        }
        const auto valid =
            validate_animation_compatibility(sequence, bone_layout->skeleton_id(), bone_layout->skeleton());
        if (!valid.succeeded())
        {
            return AssetResult<AnimationPose>(valid);
        }
        const auto sampled = animation_detail::sample_validated_pose(bone_layout->skeleton(), sequence, time);
        if (!sampled.succeeded())
        {
            return AssetResult<AnimationPose>(sampled.status());
        }
        return AssetResult<AnimationPose>(AnimationPose{std::move(bone_layout), sampled.value()});
    }

    AssetResult<std::vector<Transform>> animation_detail::sample_validated_pose(const SkeletonAssetData& skeleton,
                                                                                const AnimationSequenceAsset& sequence,
                                                                                double time)
    {
        if (!std::isfinite(time) || time < 0 || time > sequence.data.duration)
        {
            return AssetResult<std::vector<Transform>>(invalid("animation sample time is outside the clip"));
        }
        std::vector<Transform> pose;
        pose.reserve(skeleton.bones.size());
        for (const auto& bone : skeleton.bones)
        {
            pose.push_back(bone.reference_local_transform);
        }
        const auto last = sequence.data.sample_count - 1;
        const auto first = std::min(last, static_cast<std::uint32_t>(std::floor(time * sequence.data.sample_rate)));
        const auto second = std::min(last, first + 1);
        const double start_time =
            std::min(sequence.data.duration, static_cast<double>(first) / sequence.data.sample_rate);
        const double end_time =
            std::min(sequence.data.duration, static_cast<double>(second) / sequence.data.sample_rate);
        const float alpha =
            end_time > start_time ? static_cast<float>((time - start_time) / (end_time - start_time)) : 0.0f;
        for (const auto& track : sequence.tracks)
        {
            const auto& a = track.samples[first];
            const auto& b = track.samples[second];
            auto& transform = pose[track.bone_index];
            transform.translation = a.translation * (1.0f - alpha) + b.translation * alpha;
            transform.scale = a.scale * (1.0f - alpha) + b.scale * alpha;
            if (!try_slerp(a.rotation, b.rotation, alpha, transform.rotation))
            {
                return AssetResult<std::vector<Transform>>(invalid("animation rotation interpolation failed"));
            }
        }
        return AssetResult<std::vector<Transform>>(std::move(pose));
    }

    AssetResult<AnimationPose> blend_animation_poses(const AnimationPose& first, const AnimationPose& second,
                                                     double alpha)
    {
        if (!std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0)
        {
            return AssetResult<AnimationPose>(invalid("blend alpha must be finite and within zero to one"));
        }
        return blend_animation_poses(first.bone_layout, {{&first, 1.0 - alpha}, {&second, alpha}});
    }

    AssetResult<AnimationPose> blend_animation_poses(std::shared_ptr<const AnimationBoneLayout> bone_layout,
                                                     const std::vector<WeightedAnimationPose>& inputs)
    {
        auto result = make_reference_pose(std::move(bone_layout));
        if (!result.succeeded())
        {
            return result;
        }
        double maximum_weight = 0.0;
        const AnimationPose* anchor = nullptr;
        std::size_t contributing_count = 0;
        for (const auto& input : inputs)
        {
            // Even zero-weight inputs must share the domain; invalid transitions fail before publication.
            if (!input.pose || !validate_animation_pose(*input.pose).succeeded() ||
                !result.value().bone_layout->compatible(*input.pose->bone_layout) || !std::isfinite(input.weight) ||
                input.weight < 0.0)
            {
                return AssetResult<AnimationPose>(invalid("invalid weight or incompatible animation blend input"));
            }
            if (input.weight > 0.0)
            {
                ++contributing_count;
            }
            if (input.weight > maximum_weight)
            {
                maximum_weight = input.weight;
                anchor = input.pose;
            }
        }
        if (!anchor)
        {
            return result;
        }
        if (contributing_count == 1)
        {
            auto endpoint = *anchor;
            endpoint.bone_layout = result.value().bone_layout;
            return AssetResult<AnimationPose>(std::move(endpoint));
        }
        // Divide by the maximum first so a sum of large finite weights cannot overflow.
        double total = 0.0;
        for (const auto& input : inputs)
        {
            total += input.weight / maximum_weight;
        }
        AnimationPose output = result.value();
        for (std::size_t bone = 0; bone < output.local_transforms.size(); ++bone)
        {
            double translation[3]{};
            double scale[3]{};
            double rotation[4]{};
            const auto& reference_rotation = anchor->local_transforms[bone].rotation;
            for (const auto& input : inputs)
            {
                if (input.weight == 0.0)
                {
                    continue;
                }
                const double weight = (input.weight / maximum_weight) / total;
                const auto& transform = input.pose->local_transforms[bone];
                const auto& q = transform.rotation;
                const double dot =
                    static_cast<double>(q.x) * reference_rotation.x + static_cast<double>(q.y) * reference_rotation.y +
                    static_cast<double>(q.z) * reference_rotation.z + static_cast<double>(q.w) * reference_rotation.w;
                const double signed_weight = dot < 0.0 ? -weight : weight;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    translation[axis] += transform.translation.data()[axis] * weight;
                    scale[axis] += transform.scale.data()[axis] * weight;
                }
                for (std::size_t axis = 0; axis < 4; ++axis)
                {
                    rotation[axis] += q.data()[axis] * signed_weight;
                }
            }
            auto& transform = output.local_transforms[bone];
            transform.translation = Vector3(static_cast<float>(translation[0]), static_cast<float>(translation[1]),
                                            static_cast<float>(translation[2]));
            transform.scale =
                Vector3(static_cast<float>(scale[0]), static_cast<float>(scale[1]), static_cast<float>(scale[2]));
            if (!try_normalize(Quaternion(static_cast<float>(rotation[0]), static_cast<float>(rotation[1]),
                                          static_cast<float>(rotation[2]), static_cast<float>(rotation[3])),
                               transform.rotation) ||
                !validate_animation_transform(transform).succeeded())
            {
                return AssetResult<AnimationPose>(invalid("degenerate or nonfinite animation blend result"));
            }
        }
        return AssetResult<AnimationPose>(std::move(output));
    }

    AssetStatus lock_animation_root(AnimationPose& pose)
    {
        const auto valid = validate_animation_pose(pose);
        if (!valid.succeeded())
        {
            return valid;
        }
        const auto& root = pose.bone_layout->skeleton().bones[0].reference_local_transform;
        pose.local_transforms[0].translation = root.translation;
        pose.local_transforms[0].rotation = root.rotation;
        return AssetStatus::success();
    }

    AssetResult<ComponentSpacePose> build_component_space_pose(const AnimationPose& pose)
    {
        const auto valid = validate_animation_pose(pose);
        if (!valid.succeeded())
        {
            return AssetResult<ComponentSpacePose>(valid);
        }
        ComponentSpacePose component;
        component.bone_layout = pose.bone_layout;
        component.bone_matrices.resize(pose.local_transforms.size());
        for (std::size_t bone = 0; bone < pose.local_transforms.size(); ++bone)
        {
            const auto local = to_matrix(pose.local_transforms[bone]);
            const auto parent = pose.bone_layout->skeleton().bones[bone].parent_index;
            const auto matrix = parent < 0 ? local : component.bone_matrices[parent] * local;
            if (!is_finite(matrix))
            {
                return AssetResult<ComponentSpacePose>(invalid("nonfinite component pose"));
            }
            component.bone_matrices[bone] = matrix;
        }
        return AssetResult<ComponentSpacePose>(std::move(component));
    }
} // namespace toy3d
