#include "renderscene/render_frame_dispatcher.h"

#include <exception>
#include <utility>

namespace toy3d
{
    namespace
    {
        constexpr const char* stopped_message = "The RenderFrameDispatcher is not accepting frames.";
        constexpr const char* exception_message = "The RenderFrameDispatcher terminated because frame processing threw an exception.";

        void cancel_packet(RenderFramePacket& packet, const std::string& message)
        {
            if (packet.completion != nullptr)
            {
                packet.completion->cancel(message);
            }
        }
    }

    RenderFrameExecutionStatus RenderFrameExecutionStatus::success(
        RHIErrorCode rhi_error_code)
    {
        RenderFrameExecutionStatus result;
        result.rhi_error_code = rhi_error_code;
        return result;
    }

    bool RenderFrameExecutionStatus::is_fatal() const
    {
        return outcome == RenderFrameExecutionOutcome::FatalRenderer;
    }

    RenderFrameExecutionStatus RenderFrameExecutionStatus::frame_failure(
        std::string message,
        RHIErrorCode rhi_error_code)
    {
        RenderFrameExecutionStatus result;
        result.outcome = RenderFrameExecutionOutcome::FrameFailed;
        result.rhi_error_code = rhi_error_code;
        result.message = std::move(message);
        return result;
    }

    RenderFrameExecutionStatus RenderFrameExecutionStatus::fatal_failure(
        std::string message,
        RHIErrorCode rhi_error_code)
    {
        RenderFrameExecutionStatus result;
        result.outcome = RenderFrameExecutionOutcome::FatalRenderer;
        result.rhi_error_code = rhi_error_code;
        result.message = std::move(message);
        return result;
    }

    RenderFrameDispatcher::RenderFrameDispatcher(
        RenderFrameDispatcherConfig config,
        std::unique_ptr<RenderFrameProcessor> processor)
        : config_(config),
          processor_(std::move(processor))
    {
        if (!config_.multithreaded)
        {
            config_.one_frame_thread_lag = false;
        }
    }

    RenderFrameDispatcher::~RenderFrameDispatcher()
    {
        shutdown();
    }

    RenderFrameExecutionStatus RenderFrameDispatcher::start()
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ != State::Created)
            {
                return RenderFrameExecutionStatus::fatal_failure("The RenderFrameDispatcher can only be started once.");
            }
            if (processor_ == nullptr)
            {
                state_ = State::Stopped;
                return RenderFrameExecutionStatus::fatal_failure("The RenderFrameDispatcher requires a frame processor.");
            }
            state_ = State::Starting;
        }

        if (!config_.multithreaded)
        {
            const RenderFrameExecutionStatus result = invoke_initialize();
            if (!result)
            {
                const RenderFrameExecutionStatus shutdown_result = invoke_shutdown();
                processor_.reset();
                std::lock_guard<std::mutex> lock(state_mutex_);
                startup_result_ = result;
                shutdown_result_ = shutdown_result;
                state_ = State::Stopped;
                return result;
            }
            std::lock_guard<std::mutex> lock(state_mutex_);
            startup_result_ = result;
            state_ = State::Running;
            return result;
        }

        try
        {
            thread_ = std::thread(&RenderFrameDispatcher::thread_main, this);
        }
        catch (const std::exception& exception)
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_ = State::Stopped;
            startup_result_ = RenderFrameExecutionStatus::fatal_failure(exception.what());
            return startup_result_;
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_ = State::Stopped;
            startup_result_ = RenderFrameExecutionStatus::fatal_failure("The Render thread could not be created.");
            return startup_result_;
        }

        std::unique_lock<std::mutex> lock(state_mutex_);
        state_changed_.wait(lock, [this]()
        {
            return state_ != State::Starting;
        });
        const RenderFrameExecutionStatus result = startup_result_;
        lock.unlock();
        if (!result && thread_.joinable())
        {
            thread_.join();
        }
        return result;
    }

    RenderFrameSubmitResult RenderFrameDispatcher::submit(RenderFramePacket packet)
    {
        if (config_.multithreaded)
        {
            return submit_threaded(std::move(packet));
        }
        return submit_inline(std::move(packet));
    }

    RenderFrameExecutionStatus RenderFrameDispatcher::flush()
    {
        RenderFrameCompletionRef lagged_completion;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ != State::Running)
            {
                return RenderFrameExecutionStatus::fatal_failure(stopped_message);
            }
            lagged_completion = lagged_completion_;
            lagged_frame_id_ = {};
            lagged_completion_.reset();
        }
        if (lagged_completion != nullptr)
        {
            lagged_completion->wait();
        }

        if (!config_.multithreaded)
        {
            const RenderFrameExecutionStatus result = invoke_flush();
            if (result.is_fatal())
            {
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    state_ = State::Stopping;
                }
                const RenderFrameExecutionStatus shutdown_result = invoke_shutdown();
                processor_.reset();
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    shutdown_result_ = shutdown_result;
                    state_ = State::Stopped;
                }
            }
            return result;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ != State::Running)
            {
                return RenderFrameExecutionStatus::fatal_failure(stopped_message);
            }
            flush_request_.pending = true;
            flush_request_.completed = false;
            flush_request_.result = RenderFrameExecutionStatus::success();
        }
        queue_.interrupt_wait();

        std::unique_lock<std::mutex> lock(state_mutex_);
        state_changed_.wait(lock, [this]()
        {
            return flush_request_.completed || state_ != State::Running;
        });
        if (!flush_request_.completed)
        {
            return RenderFrameExecutionStatus::fatal_failure(stopped_message);
        }
        return flush_request_.result;
    }

    RenderFrameExecutionStatus RenderFrameDispatcher::shutdown()
    {
        bool join_stopped_thread = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ == State::Created)
            {
                state_ = State::Stopped;
                return RenderFrameExecutionStatus::success();
            }
            if (state_ == State::Stopped)
            {
                join_stopped_thread = thread_.joinable();
                if (!join_stopped_thread)
                {
                    return shutdown_result_;
                }
            }
            else
            {
                state_ = State::Stopping;
            }
        }

        if (join_stopped_thread)
        {
            thread_.join();
            std::lock_guard<std::mutex> lock(state_mutex_);
            return shutdown_result_;
        }

        if (!config_.multithreaded)
        {
            const RenderFrameExecutionStatus result = invoke_shutdown();
            processor_.reset();
            std::lock_guard<std::mutex> lock(state_mutex_);
            shutdown_result_ = result;
            state_ = State::Stopped;
            return result;
        }

        queue_.stop_accepting();
        if (thread_.joinable())
        {
            thread_.join();
        }
        std::lock_guard<std::mutex> lock(state_mutex_);
        return shutdown_result_;
    }

    const RenderFrameDispatcherConfig& RenderFrameDispatcher::config() const
    {
        return config_;
    }

    bool RenderFrameDispatcher::is_running() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return state_ == State::Running;
    }

    RenderFrameSubmitResult RenderFrameDispatcher::submit_inline(RenderFramePacket packet)
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ != State::Running)
            {
                cancel_packet(packet, stopped_message);
                return {};
            }
        }

        const RenderFrameId frame_id = packet.frame_id;
        const RenderFrameCompletionRef completion = packet.completion;
        if (!is_valid_render_frame_packet(packet))
        {
            cancel_packet(packet, "The RenderFramePacket is invalid.");
            RenderFrameSubmitResult result;
            result.enqueue_result = RenderFrameEnqueueResult::InvalidPacket;
            return result;
        }
        const RenderFrameExecutionStatus status = process_packet(packet);
        if (status.is_fatal())
        {
            const RenderFrameExecutionStatus shutdown_result = invoke_shutdown();
            processor_.reset();
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                shutdown_result_ = shutdown_result;
                state_ = State::Stopped;
            }
        }
        return wait_for_frame(RenderFrameEnqueueResult::Accepted, frame_id, completion);
    }

    RenderFrameSubmitResult RenderFrameDispatcher::submit_threaded(RenderFramePacket packet)
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ != State::Running)
            {
                cancel_packet(packet, stopped_message);
                return {};
            }
        }

        const RenderFrameId frame_id = packet.frame_id;
        const RenderFrameCompletionRef completion = packet.completion;
        const RenderFrameEnqueueResult enqueue_result = queue_.enqueue(std::move(packet));
        if (enqueue_result != RenderFrameEnqueueResult::Accepted)
        {
            RenderFrameSubmitResult result;
            result.enqueue_result = enqueue_result;
            return result;
        }

        if (!config_.one_frame_thread_lag)
        {
            return wait_for_frame(enqueue_result, frame_id, completion);
        }

        RenderFrameId completed_frame_id;
        RenderFrameCompletionRef completed_frame;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            completed_frame_id = lagged_frame_id_;
            completed_frame = lagged_completion_;
            lagged_frame_id_ = frame_id;
            lagged_completion_ = completion;
        }
        if (completed_frame == nullptr)
        {
            RenderFrameSubmitResult result;
            result.enqueue_result = enqueue_result;
            return result;
        }
        return wait_for_frame(enqueue_result, completed_frame_id, completed_frame);
    }

    RenderFrameExecutionStatus RenderFrameDispatcher::process_packet(RenderFramePacket& packet)
    {
        RenderFrameExecutionStatus status;
        try
        {
            status = processor_->process_frame(packet);
        }
        catch (...)
        {
            status = RenderFrameExecutionStatus::fatal_failure(exception_message);
        }

        if (status)
        {
            packet.completion->complete_success(status.rhi_error_code);
        }
        else
        {
            if (status.is_fatal())
            {
                fail_thread(status.message);
                packet.completion->complete_fatal(
                    status.message,
                    status.rhi_error_code);
            }
            else
            {
                packet.completion->complete_failure(
                    status.message,
                    status.rhi_error_code);
            }
        }
        return status;
    }

    RenderFrameSubmitResult RenderFrameDispatcher::wait_for_frame(
        RenderFrameEnqueueResult enqueue_result,
        RenderFrameId frame_id,
        const RenderFrameCompletionRef& completion) const
    {
        RenderFrameSubmitResult result;
        result.enqueue_result = enqueue_result;
        result.completed_frame_id = frame_id;
        if (completion != nullptr)
        {
            result.completed_frame = completion->wait();
        }
        return result;
    }

    void RenderFrameDispatcher::thread_main()
    {
        const RenderFrameExecutionStatus startup_result = invoke_initialize();
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            startup_result_ = startup_result;
            state_ = startup_result ? State::Running : State::Stopping;
        }
        state_changed_.notify_all();

        if (startup_result)
        {
            try
            {
                while (true)
                {
                    RenderFramePacket packet;
                    const RenderFrameDequeueResult dequeue_result = queue_.wait_dequeue(packet);
                    if (dequeue_result == RenderFrameDequeueResult::Packet)
                    {
                        if (process_packet(packet).is_fatal())
                        {
                            break;
                        }
                        continue;
                    }
                    if (dequeue_result == RenderFrameDequeueResult::Interrupted)
                    {
                        bool flush_pending = false;
                        {
                            std::lock_guard<std::mutex> lock(state_mutex_);
                            flush_pending = flush_request_.pending;
                            flush_request_.pending = false;
                        }
                        if (flush_pending)
                        {
                            const RenderFrameExecutionStatus flush_result = invoke_flush();
                            if (flush_result.is_fatal())
                            {
                                fail_thread(flush_result.message);
                            }
                            complete_pending_flush(flush_result);
                            if (flush_result.is_fatal())
                            {
                                break;
                            }
                        }
                        continue;
                    }
                    break;
                }
            }
            catch (...)
            {
                fail_thread(exception_message);
            }
        }

        const RenderFrameExecutionStatus shutdown_result = invoke_shutdown();
        processor_.reset();
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            shutdown_result_ = shutdown_result;
            if (flush_request_.pending && !flush_request_.completed)
            {
                flush_request_.pending = false;
                flush_request_.completed = true;
                flush_request_.result = RenderFrameExecutionStatus::fatal_failure(stopped_message);
            }
            state_ = State::Stopped;
        }
        state_changed_.notify_all();
    }

    void RenderFrameDispatcher::fail_thread(std::string message)
    {
        queue_.abort_pending(message);
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = State::Stopping;
    }

    void RenderFrameDispatcher::complete_pending_flush(RenderFrameExecutionStatus result)
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            flush_request_.completed = true;
            flush_request_.result = std::move(result);
        }
        state_changed_.notify_all();
    }

    RenderFrameExecutionStatus RenderFrameDispatcher::invoke_initialize()
    {
        try
        {
            return processor_->initialize();
        }
        catch (const std::exception& exception)
        {
            return RenderFrameExecutionStatus::fatal_failure(exception.what());
        }
        catch (...)
        {
            return RenderFrameExecutionStatus::fatal_failure("The RenderFrameDispatcher processor failed during initialization.");
        }
    }

    RenderFrameExecutionStatus RenderFrameDispatcher::invoke_flush()
    {
        try
        {
            return processor_->flush();
        }
        catch (const std::exception& exception)
        {
            return RenderFrameExecutionStatus::fatal_failure(exception.what());
        }
        catch (...)
        {
            return RenderFrameExecutionStatus::fatal_failure("The RenderFrameDispatcher processor failed while flushing.");
        }
    }

    RenderFrameExecutionStatus RenderFrameDispatcher::invoke_shutdown()
    {
        try
        {
            return processor_->shutdown();
        }
        catch (const std::exception& exception)
        {
            return RenderFrameExecutionStatus::fatal_failure(exception.what());
        }
        catch (...)
        {
            return RenderFrameExecutionStatus::fatal_failure("The RenderFrameDispatcher processor failed during shutdown.");
        }
    }
}
