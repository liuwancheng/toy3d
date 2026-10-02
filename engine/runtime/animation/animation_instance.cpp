#include "animation_instance.h"

#include <cmath>
#include <limits>

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
    // AnimationInstance: sequence state update, local blending and cached evaluation
    // --------------------------------------------------------------------------
    AssetStatus AnimationInstance::invalidate()
    {
        if (revision_ == std::numeric_limits<std::uint64_t>::max())
        {
            return invalid("animation pose revision overflow");
        }
        ++revision_;
        cached_evaluation_.reset();
        return AssetStatus::success();
    }

    AssetStatus AnimationInstance::set_sources(std::shared_ptr<const AnimationBoneLayout> bone_layout,
                                               const std::vector<AnimationSequenceInput>& inputs)
    {
        if (!bone_layout || !bone_layout->status().succeeded())
        {
            return invalid("animation instance requires a valid bone layout");
        }
        std::vector<std::shared_ptr<const AnimationSequence>> sequences;
        std::vector<SequencePlaybackState> states;
        for (const auto& input : inputs)
        {
            if (!input.sequence)
            {
                return invalid("animation instance sequence is missing");
            }
            if (!input.sequence->status().succeeded())
            {
                return input.sequence->status();
            }
            if (!bone_layout->compatible(*input.sequence->bone_layout()))
            {
                return invalid("animation sequence belongs to a different bone layout");
            }
            SequencePlaybackState state;
            const auto initialized = state.initialize(input.sequence->duration(), input.playback);
            if (!initialized.succeeded())
            {
                return initialized;
            }
            sequences.push_back(input.sequence);
            states.push_back(state);
        }
        const auto valid = invalidate();
        if (!valid.succeeded())
        {
            return valid;
        }
        bone_layout_ = std::move(bone_layout);
        sequences_ = std::move(sequences);
        playback_states_ = std::move(states);
        weights_.assign(inputs.size(), 0.0);
        if (!weights_.empty())
        {
            weights_[0] = 1.0;
        }
        lock_root_ = false;
        return AssetStatus::success();
    }

    AssetStatus AnimationInstance::update(const AnimationUpdateInput& input)
    {
        if (!bone_layout_ || input.weights.size() != sequences_.size() || !std::isfinite(input.delta_time) ||
            input.delta_time < 0.0)
        {
            return invalid("animation update has invalid time or source weight count");
        }
        for (const auto weight : input.weights)
        {
            if (!std::isfinite(weight) || weight < 0.0)
            {
                return invalid("animation source weights must be finite and nonnegative");
            }
        }
        auto states = playback_states_;
        for (auto& state : states)
        {
            // All explicitly configured sources advance, including zero-weight sources.
            // A future state machine decides relevance/reentry through source controls.
            const auto advanced = state.advance(input.delta_time);
            if (!advanced.succeeded())
            {
                return advanced;
            }
        }
        const auto valid = invalidate();
        if (!valid.succeeded())
        {
            return valid;
        }
        playback_states_ = std::move(states);
        weights_ = input.weights;
        lock_root_ = input.lock_root;
        return AssetStatus::success();
    }

    AssetStatus AnimationInstance::seek(std::size_t source, double time)
    {
        if (source >= playback_states_.size())
        {
            return invalid("animation seek source is out of range");
        }
        auto state = playback_states_[source];
        const auto valid = state.seek(time);
        if (!valid.succeeded())
        {
            return valid;
        }
        const auto changed = invalidate();
        if (!changed.succeeded())
        {
            return changed;
        }
        playback_states_[source] = state;
        return AssetStatus::success();
    }

    AssetStatus AnimationInstance::set_playback_settings(std::size_t source, const AnimationPlaybackSettings& settings)
    {
        if (source >= playback_states_.size())
        {
            return invalid("animation settings source is out of range");
        }
        auto state = playback_states_[source];
        const auto valid = state.set_settings(settings);
        if (!valid.succeeded())
        {
            return valid;
        }
        playback_states_[source] = state;
        return AssetStatus::success();
    }

    AssetStatus AnimationInstance::set_playing(std::size_t source, bool playing)
    {
        if (source >= playback_states_.size())
        {
            return invalid("animation playback source is out of range");
        }
        if (playing)
        {
            playback_states_[source].play();
        }
        else
        {
            playback_states_[source].pause();
        }
        return AssetStatus::success();
    }

    AssetResult<std::shared_ptr<const AnimationEvaluation>> AnimationInstance::evaluate() const
    {
        if (!bone_layout_)
        {
            return AssetResult<std::shared_ptr<const AnimationEvaluation>>(
                invalid("animation instance has no sources"));
        }
        if (cached_evaluation_)
        {
            return AssetResult<std::shared_ptr<const AnimationEvaluation>>(cached_evaluation_);
        }
        std::vector<AnimationPose> poses;
        poses.reserve(sequences_.size());
        for (std::size_t source = 0; source < sequences_.size(); ++source)
        {
            const auto sampled = sequences_[source]->sample(playback_states_[source].time());
            if (!sampled.succeeded())
            {
                return AssetResult<std::shared_ptr<const AnimationEvaluation>>(sampled.status());
            }
            poses.push_back(sampled.value());
        }
        std::vector<WeightedAnimationPose> inputs;
        for (std::size_t source = 0; source < poses.size(); ++source)
        {
            inputs.push_back({&poses[source], weights_[source]});
        }
        const auto blended = blend_animation_poses(bone_layout_, inputs);
        if (!blended.succeeded())
        {
            return AssetResult<std::shared_ptr<const AnimationEvaluation>>(blended.status());
        }
        AnimationEvaluation evaluation;
        evaluation.revision = revision_;
        evaluation.local_pose = blended.value();
        if (lock_root_)
        {
            const auto valid = lock_animation_root(evaluation.local_pose);
            if (!valid.succeeded())
            {
                return AssetResult<std::shared_ptr<const AnimationEvaluation>>(valid);
            }
        }
        const auto component = build_component_space_pose(evaluation.local_pose);
        if (!component.succeeded())
        {
            return AssetResult<std::shared_ptr<const AnimationEvaluation>>(component.status());
        }
        evaluation.component_pose = component.value();
        cached_evaluation_ = std::make_shared<const AnimationEvaluation>(std::move(evaluation));
        return AssetResult<std::shared_ptr<const AnimationEvaluation>>(cached_evaluation_);
    }

    const SequencePlaybackState* AnimationInstance::playback_state(std::size_t source) const
    {
        return source < playback_states_.size() ? &playback_states_[source] : nullptr;
    }

    const std::shared_ptr<const AnimationBoneLayout>& AnimationInstance::bone_layout() const
    {
        return bone_layout_;
    }
} // namespace toy3d
