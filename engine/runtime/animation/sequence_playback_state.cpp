#include "sequence_playback_state.h"

#include <algorithm>
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
    // SequencePlaybackState: independent node clock and continuous update interval
    // --------------------------------------------------------------------------
    AssetStatus SequencePlaybackState::initialize(double duration, const AnimationPlaybackSettings& settings)
    {
        if (!std::isfinite(duration) || duration < 0.0 || duration > max_animation_duration)
        {
            return invalid("invalid animation playback duration");
        }
        const auto valid = set_settings(settings);
        if (!valid.succeeded())
        {
            return valid;
        }
        duration_ = duration;
        time_ = 0.0;
        interval_ = {};
        playing_ = settings_.autoplay && duration_ > 0.0;
        return AssetStatus::success();
    }

    AssetStatus SequencePlaybackState::set_settings(const AnimationPlaybackSettings& settings)
    {
        if (!std::isfinite(settings.rate) || settings.rate <= 0.0)
        {
            return invalid("animation playback rate must be finite and positive");
        }
        settings_ = settings;
        return AssetStatus::success();
    }

    AssetStatus SequencePlaybackState::seek(double time)
    {
        if (!std::isfinite(time) || time < 0.0 || time > duration_)
        {
            return invalid("animation seek is outside the clip");
        }
        interval_ = {time_, time, 0.0, 0, true};
        time_ = time;
        return AssetStatus::success();
    }

    AssetStatus SequencePlaybackState::advance(double delta_time)
    {
        if (!std::isfinite(delta_time) || delta_time < 0.0)
        {
            return invalid("animation delta time must be finite and nonnegative");
        }
        SequencePlaybackInterval next{time_, time_, 0.0, 0, false};
        bool next_playing = playing_;
        if (playing_ && delta_time > 0.0)
        {
            const double delta = delta_time * settings_.rate;
            if (!std::isfinite(delta))
            {
                return invalid("animation clock overflow");
            }
            if (delta == 0.0)
            {
                // A positive input step can underflow after applying the rate. It must not
                // wrap a sought end frame or report events without any actual advancement.
                interval_ = next;
                return AssetStatus::success();
            }
            if (settings_.loop)
            {
                // Derive the quotient from the same remainder used for the clock. A separate
                // floor(delta / duration) can round up at decimal durations and count an extra loop.
                const double remainder = std::fmod(delta, duration_);
                const double whole_loops = std::round((delta - remainder) / duration_);
                if (!std::isfinite(whole_loops) || whole_loops >= std::ldexp(1.0, 64))
                {
                    return invalid("animation loop count overflow");
                }
                next.loops_crossed = static_cast<std::uint64_t>(whole_loops);
                const bool wraps = time_ >= duration_ - remainder;
                if (wraps && next.loops_crossed == std::numeric_limits<std::uint64_t>::max())
                {
                    return invalid("animation loop count overflow");
                }
                next.loops_crossed += wraps ? 1 : 0;
                next.current_time = wraps ? time_ - (duration_ - remainder) : time_ + remainder;
                next.advanced_time = delta;
            }
            else
            {
                const double remaining = duration_ - time_;
                next.advanced_time = std::min(delta, remaining);
                next.current_time = time_ + next.advanced_time;
                if (delta >= remaining)
                {
                    next.current_time = duration_;
                    next_playing = false;
                }
            }
        }
        interval_ = next;
        time_ = next.current_time;
        playing_ = next_playing;
        return AssetStatus::success();
    }

    void SequencePlaybackState::play()
    {
        playing_ = duration_ > 0.0;
    }

    void SequencePlaybackState::pause()
    {
        playing_ = false;
    }

    double SequencePlaybackState::time() const
    {
        return time_;
    }

    double SequencePlaybackState::remaining_time() const
    {
        return (duration_ - time_) / settings_.rate;
    }

    bool SequencePlaybackState::playing() const
    {
        return playing_;
    }

    const AnimationPlaybackSettings& SequencePlaybackState::settings() const
    {
        return settings_;
    }

    const SequencePlaybackInterval& SequencePlaybackState::interval() const
    {
        return interval_;
    }
} // namespace toy3d
