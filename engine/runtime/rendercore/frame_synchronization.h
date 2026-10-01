#pragma once

#include "threading/task_graph/graph_event.h"
#include "threading/task_graph/task_graph_types.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace toy3d
{
    class TaskGraphInterface;

    // Immutable bridge result. It reports RT CPU progress separately from Task Graph
    // failure and a renderer terminal diagnostic, and never owns Renderer or RHI state.
    struct RenderFenceWaitResult
    {
      public:
        static RenderFenceWaitResult reached();
        static RenderFenceWaitResult framework_failure(TaskGraphStatus status);
        static RenderFenceWaitResult renderer_terminal(std::string error_message);

        bool succeeded() const noexcept;
        bool rendering_thread_reached() const noexcept;
        bool has_renderer_terminal() const noexcept;
        const TaskGraphStatus& framework_status() const noexcept;
        const std::string& renderer_error() const noexcept;

      private:
        bool rendering_thread_reached_ = false;
        bool renderer_terminal_ = false;
        TaskGraphStatus framework_status_;
        std::string renderer_error_;
    };

    // GT-owned CPU fence. The stored Task Graph pointer is non-owning and is valid only
    // from begin_fence() through wait() inside the composition-root-controlled lifetime.
    class RenderCommandFence final
    {
      public:
        RenderCommandFence() = default;

        RenderCommandFence(const RenderCommandFence&) = delete;
        RenderCommandFence& operator=(const RenderCommandFence&) = delete;

        TaskGraphStatus begin_fence();
        RenderFenceWaitResult wait(const std::function<RenderFenceWaitResult()>& status_provider = {}) const;
        bool is_complete() const noexcept;

      private:
        TaskGraphInterface* task_graph_ = nullptr;
        GraphEventRef completion_event_;
    };

    // Engine-owned, GT-only frame-lag policy. The two fences rotate in one-frame-lag
    // mode; zero-lag mode begins and waits the same fence each frame.
    class FrameEndSync final
    {
      public:
        explicit FrameEndSync(bool allow_one_frame_thread_lag = true,
                              std::function<RenderFenceWaitResult()> status_provider = {});

        FrameEndSync(const FrameEndSync&) = delete;
        FrameEndSync& operator=(const FrameEndSync&) = delete;

        RenderFenceWaitResult sync_frame();

      private:
        std::array<RenderCommandFence, 2> fences_;
        const bool allow_one_frame_thread_lag_ = true;
        std::function<RenderFenceWaitResult()> status_provider_;
        std::size_t fence_index_ = 0;
    };

    RenderFenceWaitResult flush_rendering_commands(const std::function<RenderFenceWaitResult()>& status_provider = {});
} // namespace toy3d
