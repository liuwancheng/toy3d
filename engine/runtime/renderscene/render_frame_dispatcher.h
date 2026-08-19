#pragma once

#include "renderscene/render_frame_queue.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace toy3d
{
    enum class RenderFrameExecutionOutcome
    {
        Succeeded,
        FrameFailed,
        FatalRenderer
    };

    struct RenderFrameExecutionStatus
    {
        RenderFrameExecutionOutcome outcome = RenderFrameExecutionOutcome::Succeeded;
        std::string message;

        explicit operator bool() const
        {
            return outcome == RenderFrameExecutionOutcome::Succeeded;
        }

        bool is_fatal() const;

        static RenderFrameExecutionStatus success();
        static RenderFrameExecutionStatus frame_failure(std::string message);
        static RenderFrameExecutionStatus fatal_failure(std::string message);
    };

    class RenderFrameProcessor
    {
    public:
        virtual ~RenderFrameProcessor() = default;

        // RenderFrameDispatcher owns the processor and calls every phase on the selected
        // rendering execution thread. process_frame reports work outcome; RenderFrameDispatcher
        // alone publishes the packet completion.
        virtual RenderFrameExecutionStatus initialize() = 0;
        virtual RenderFrameExecutionStatus process_frame(const RenderFramePacket& packet) = 0;
        virtual RenderFrameExecutionStatus flush() = 0;
        virtual RenderFrameExecutionStatus shutdown() = 0;
    };

    struct RenderFrameDispatcherConfig
    {
        bool multithreaded = true;
        bool one_frame_thread_lag = true;
    };

    struct RenderFrameSubmitResult
    {
        RenderFrameEnqueueResult enqueue_result = RenderFrameEnqueueResult::Stopped;
        RenderFrameId completed_frame_id;
        RenderFrameCompletionResult completed_frame;

        bool has_completed_frame() const
        {
            return static_cast<bool>(completed_frame_id);
        }
    };

    class RenderFrameDispatcher final
    {
    public:
        RenderFrameDispatcher(
            RenderFrameDispatcherConfig config,
            std::unique_ptr<RenderFrameProcessor> processor);
        ~RenderFrameDispatcher();

        RenderFrameDispatcher(const RenderFrameDispatcher&) = delete;
        RenderFrameDispatcher& operator=(const RenderFrameDispatcher&) = delete;

        // Lifecycle and submission calls are serialized by the Main Thread.
        // The internal mutex coordinates with the Render Thread; it does not
        // make concurrent Main-Thread callers part of the public contract.
        RenderFrameExecutionStatus start();
        RenderFrameSubmitResult submit(RenderFramePacket packet);
        RenderFrameExecutionStatus flush();
        RenderFrameExecutionStatus shutdown();

        const RenderFrameDispatcherConfig& config() const;
        bool is_running() const;

    private:
        enum class State
        {
            Created,
            Starting,
            Running,
            Stopping,
            Stopped
        };

        struct FlushRequest
        {
            bool pending = false;
            bool completed = false;
            RenderFrameExecutionStatus result;
        };

        RenderFrameSubmitResult submit_inline(RenderFramePacket packet);
        RenderFrameSubmitResult submit_threaded(RenderFramePacket packet);
        RenderFrameExecutionStatus process_packet(RenderFramePacket& packet);
        RenderFrameSubmitResult wait_for_frame(
            RenderFrameEnqueueResult enqueue_result,
            RenderFrameId frame_id,
            const RenderFrameCompletionRef& completion) const;
        void thread_main();
        void fail_thread(std::string message);
        void complete_pending_flush(RenderFrameExecutionStatus result);
        RenderFrameExecutionStatus invoke_initialize();
        RenderFrameExecutionStatus invoke_flush();
        RenderFrameExecutionStatus invoke_shutdown();

        RenderFrameDispatcherConfig config_;
        std::unique_ptr<RenderFrameProcessor> processor_;
        RenderFrameQueue queue_;
        std::thread thread_;

        mutable std::mutex state_mutex_;
        std::condition_variable state_changed_;
        State state_ = State::Created;
        RenderFrameExecutionStatus startup_result_;
        RenderFrameExecutionStatus shutdown_result_;
        FlushRequest flush_request_;

        RenderFrameId lagged_frame_id_;
        RenderFrameCompletionRef lagged_completion_;
    };
}
