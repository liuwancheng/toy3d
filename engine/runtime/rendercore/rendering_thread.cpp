#include "rendercore/rendering_thread.h"

#include "rendercore/render_command.h"
#include "rendercore/render_command_internal.h"
#include "threading/task_graph/graph_task.h"
#include "threading/task_graph/task_graph_interface.h"
#include "threading/event.h"
#include "threading/thread.h"
#include "threading/thread_manager.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace toy3d
{
    RenderingThread::RenderingThread(ThreadManager& thread_manager, TaskGraphInterface& task_graph,
                                     RenderingThreadMode mode,
                                     std::function<std::unique_ptr<Thread>(std::function<void()>)> thread_factory)
        : thread_manager_(thread_manager), task_graph_(task_graph), mode_(mode),
          thread_factory_(std::move(thread_factory))
    {
    }

    RenderingThread::~RenderingThread()
    {
        static_cast<void>(stop());
    }

    ThreadStatus RenderingThread::start(const std::function<ThreadStatus()>& bootstrap)
    {
        if (started_)
        {
            return ThreadStatus::failure(ThreadErrorCode::InvalidState, "RenderingThread is already started");
        }

        if (mode_ == RenderingThreadMode::SingleThread)
        {
            if (task_graph_.get_render_thread() != NamedThread::GameThread ||
                task_graph_.get_current_thread_if_known() != NamedThread::GameThread)
            {
                return ThreadStatus::failure(ThreadErrorCode::InvalidCaller,
                                             "Single-thread RenderingThread must start on the attached GameThread");
            }

            const ThreadStatus bootstrap_status = run_callback(bootstrap, "bootstrap");
            if (!bootstrap_status.succeeded())
            {
                return bootstrap_status;
            }
            const TaskGraphStatus facade_status = render_command_detail::enable_render_command_facade(task_graph_);
            if (!facade_status.succeeded())
            {
                return ThreadStatus::failure(ThreadErrorCode::InvalidState, facade_status.message);
            }
            started_ = true;
            ready_.store(true);
            return ThreadStatus::success();
        }

        if (task_graph_.get_render_thread() != NamedThread::RenderingThread)
        {
            return ThreadStatus::failure(ThreadErrorCode::InvalidConfig,
                                         "Multi-thread RenderingThread requires a multi-thread Task Graph");
        }

        Event start_complete(EventMode::ManualReset);
        ThreadStatus start_status =
            ThreadStatus::failure(ThreadErrorCode::InitFailed, "RenderingThread did not publish its start result");
        std::function<void()> thread_main = [this, bootstrap, &start_complete, &start_status]()
        {
            const TaskGraphStatus attached = task_graph_.attach_to_thread(NamedThread::RenderingThread);
            if (!attached.succeeded())
            {
                start_status = ThreadStatus::failure(ThreadErrorCode::InitFailed,
                                                     "RenderingThread attach failed: " + attached.message);
                start_complete.trigger();
                return;
            }

            start_status = run_callback(bootstrap, "bootstrap");
            if (!start_status.succeeded())
            {
                start_complete.trigger();
                return;
            }

            const TaskGraphStatus facade_status = render_command_detail::enable_render_command_facade(task_graph_);
            if (!facade_status.succeeded())
            {
                start_status = ThreadStatus::failure(ThreadErrorCode::InvalidState, facade_status.message);
                start_complete.trigger();
                return;
            }

            ready_.store(true);
            start_complete.trigger();
            task_graph_.process_thread_until_request_return(NamedThread::RenderingThread);
        };

        try
        {
            thread_ = thread_factory_
                          ? thread_factory_(std::move(thread_main))
                          : std::make_unique<Thread>(thread_manager_, "RenderingThread", std::move(thread_main));
        }
        catch (const std::exception& exception)
        {
            return ThreadStatus::failure(ThreadErrorCode::CreateFailed, exception.what());
        }
        catch (...)
        {
            return ThreadStatus::failure(ThreadErrorCode::CreateFailed,
                                         "unknown exception while creating RenderingThread");
        }

        if (!thread_)
        {
            return ThreadStatus::failure(ThreadErrorCode::CreateFailed, "RenderingThread factory returned no thread");
        }

        start_complete.wait();
        if (!start_status.succeeded())
        {
            try
            {
                thread_->join();
            }
            catch (const std::exception& exception)
            {
                thread_.reset();
                return ThreadStatus::failure(ThreadErrorCode::InvalidState, exception.what());
            }
            thread_.reset();
            ready_.store(false);
            return start_status;
        }

        started_ = true;
        return ThreadStatus::success();
    }

    ThreadStatus RenderingThread::stop(const std::function<ThreadStatus()>& teardown)
    {
        if (!started_)
        {
            return ThreadStatus::success();
        }

        // Stop producer acceptance before the FIFO teardown marker is queued. Commands
        // accepted before this point remain ordered ahead of teardown on the named queue.
        render_command_detail::disable_render_command_facade(task_graph_);

        if (mode_ == RenderingThreadMode::SingleThread)
        {
            const ThreadStatus teardown_status = run_callback(teardown, "teardown");
            ready_.store(false);
            started_ = false;
            return teardown_status;
        }

        ThreadStatus teardown_status = ThreadStatus::success();
        try
        {
            GraphEventRef teardown_complete = dispatch_graph_task(
                task_graph_, "RenderingThreadTeardown",
                [this, teardown, &teardown_status](NamedThread, const GraphEventRef&)
                {
                    teardown_status = run_callback(teardown, "teardown");
                    ready_.store(false);
                    task_graph_.request_return(NamedThread::RenderingThread);
                },
                NamedThread::RenderingThread);
            const TaskWaitResult waited =
                task_graph_.wait_until_task_completes(teardown_complete, NamedThread::GameThread);
            if (!waited.succeeded() && teardown_status.succeeded())
            {
                teardown_status = ThreadStatus::failure(
                    ThreadErrorCode::InvalidState, "RenderingThread teardown task failed: " + waited.status.message);
            }
        }
        catch (const std::exception& exception)
        {
            ready_.store(false);
            task_graph_.request_return(NamedThread::RenderingThread);
            teardown_status = ThreadStatus::failure(ThreadErrorCode::InvalidState, exception.what());
        }
        catch (...)
        {
            ready_.store(false);
            task_graph_.request_return(NamedThread::RenderingThread);
            teardown_status = ThreadStatus::failure(ThreadErrorCode::InvalidState,
                                                    "unknown exception while stopping RenderingThread");
        }

        try
        {
            if (thread_)
            {
                thread_->join();
            }
        }
        catch (const std::exception& exception)
        {
            if (teardown_status.succeeded())
            {
                teardown_status = ThreadStatus::failure(ThreadErrorCode::InvalidState, exception.what());
            }
        }
        thread_.reset();
        ready_.store(false);
        started_ = false;
        return teardown_status;
    }

    bool RenderingThread::is_ready() const noexcept
    {
        return ready_.load();
    }

    RenderingThreadMode RenderingThread::get_mode() const noexcept
    {
        return mode_;
    }

    std::thread::id RenderingThread::get_thread_id() const noexcept
    {
        return thread_ ? thread_->get_thread_id() : std::thread::id{};
    }

    ThreadStatus RenderingThread::run_callback(const std::function<ThreadStatus()>& callback,
                                               const char* phase) const noexcept
    {
        if (!callback)
        {
            return ThreadStatus::success();
        }
        try
        {
            return callback();
        }
        catch (const std::exception& exception)
        {
            return ThreadStatus::failure(ThreadErrorCode::UnhandledException,
                                         std::string("RenderingThread ") + phase + " threw: " + exception.what());
        }
        catch (...)
        {
            return ThreadStatus::failure(ThreadErrorCode::UnhandledException,
                                         std::string("RenderingThread ") + phase + " threw an unknown exception");
        }
    }
} // namespace toy3d
