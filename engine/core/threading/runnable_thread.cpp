#include "threading/runnable_thread.h"

#include "threading/thread_manager.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace toy3d
{
    RunnableThreadCreateResult::RunnableThreadCreateResult(ThreadStatus status, std::unique_ptr<RunnableThread> thread)
        : status_(std::move(status)), thread_(std::move(thread))
    {
    }

    bool RunnableThreadCreateResult::succeeded() const
    {
        return status_.succeeded() && thread_ != nullptr;
    }

    const ThreadStatus& RunnableThreadCreateResult::status() const
    {
        return status_;
    }

    std::unique_ptr<RunnableThread> RunnableThreadCreateResult::take_thread()
    {
        return std::move(thread_);
    }

    RunnableThreadCreateResult RunnableThread::create(ThreadManager& thread_manager, std::unique_ptr<Runnable> runnable,
                                                      RunnableThreadConfig config)
    {
        if (!runnable)
        {
            return {ThreadStatus::failure(ThreadErrorCode::InvalidConfig, "RunnableThread requires a runnable"),
                    nullptr};
        }
        if (config.name.empty())
        {
            return {ThreadStatus::failure(ThreadErrorCode::InvalidConfig, "RunnableThread requires a non-empty name"),
                    nullptr};
        }

        std::unique_ptr<RunnableThread> created;
        try
        {
            created = std::make_unique<RunnableThread>(thread_manager, std::move(runnable), std::move(config));
        }
        catch (const std::exception& exception)
        {
            return {ThreadStatus::failure(ThreadErrorCode::CreateFailed, exception.what()), nullptr};
        }
        catch (...)
        {
            return {ThreadStatus::failure(ThreadErrorCode::CreateFailed, "unknown exception while creating thread"),
                    nullptr};
        }

        {
            std::unique_lock<std::mutex> lock(created->state_mutex_);
            created->init_condition_.wait(lock, [&created]() { return created->init_complete_; });
            if (!created->init_status_.succeeded())
            {
                const ThreadStatus failure = created->init_status_;
                lock.unlock();
                created->wait_for_completion();
                return {failure, nullptr};
            }
        }
        return {ThreadStatus::success(), std::move(created)};
    }

    RunnableThread::RunnableThread(ThreadManager& thread_manager, std::unique_ptr<Runnable> runnable,
                                   RunnableThreadConfig config)
        : thread_manager_(thread_manager), runnable_(std::move(runnable)), name_(std::move(config.name))
    {
        if (!runnable_ || name_.empty())
        {
            throw std::invalid_argument("RunnableThread must be constructed with a runnable and name");
        }
        thread_ = std::thread(&RunnableThread::thread_entry, this);
        thread_id_ = thread_.get_id();
        try
        {
            thread_manager_.add_thread(*this);
            registered_ = true;
        }
        catch (...)
        {
            request_stop();
            thread_.join();
            throw;
        }
    }

    RunnableThread::~RunnableThread()
    {
        if (joinable())
        {
            thread_manager_.report(ThreadStatus::failure(
                ThreadErrorCode::NotJoined, "thread '" + name_ + "' was destroyed before wait_for_completion"));
            request_stop();
            wait_for_completion();
        }
    }

    void RunnableThread::request_stop()
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (stop_requested_)
            {
                return;
            }
            stop_requested_ = true;
            if (state_ == RunnableThreadState::Running)
            {
                state_ = RunnableThreadState::StopRequested;
            }
        }

        try
        {
            runnable_->stop();
        }
        catch (...)
        {
            record_exception("stop");
        }
    }

    ThreadStatus RunnableThread::wait_for_completion()
    {
        // Reject before taking the join mutex: another thread may already be waiting while
        // the target detects an accidental self-join, and the target must never block on it.
        if (std::this_thread::get_id() == thread_id_)
        {
            return ThreadStatus::failure(ThreadErrorCode::InvalidCaller, "a thread cannot wait for itself");
        }

        std::lock_guard<std::mutex> join_lock(join_mutex_);
        if (!thread_.joinable())
        {
            return ThreadStatus::success();
        }
        if (thread_.joinable())
        {
            thread_.join();
        }

        bool remove_registration = false;
        {
            std::lock_guard<std::mutex> state_lock(state_mutex_);
            remove_registration = registered_;
            registered_ = false;
            state_ = RunnableThreadState::Joined;
        }
        if (remove_registration)
        {
            thread_manager_.remove_thread(*this);
        }
        return ThreadStatus::success();
    }

    bool RunnableThread::joinable() const
    {
        std::lock_guard<std::mutex> lock(join_mutex_);
        return thread_.joinable();
    }

    std::thread::id RunnableThread::get_thread_id() const
    {
        return thread_id_;
    }

    const std::string& RunnableThread::get_thread_name() const
    {
        return name_;
    }

    RunnableThreadState RunnableThread::get_state() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return state_;
    }

    ThreadExecutionResult RunnableThread::get_result() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return result_;
    }

    void RunnableThread::thread_entry()
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_ = RunnableThreadState::Initializing;
        }

        ThreadStatus init_status;
        try
        {
            init_status = runnable_->init();
        }
        catch (...)
        {
            init_status = ThreadStatus::failure(ThreadErrorCode::UnhandledException, "unhandled exception during init");
        }

        if (!init_status.succeeded())
        {
            if (init_status.code != ThreadErrorCode::UnhandledException)
            {
                init_status.code = ThreadErrorCode::InitFailed;
            }
            publish_init(std::move(init_status));
            return;
        }

        publish_init(ThreadStatus::success());
        try
        {
            const std::uint32_t return_code = runnable_->run();
            std::lock_guard<std::mutex> lock(state_mutex_);
            result_.return_code = return_code;
        }
        catch (...)
        {
            record_exception("run");
        }

        try
        {
            runnable_->exit();
        }
        catch (...)
        {
            record_exception("exit");
        }

        std::lock_guard<std::mutex> lock(state_mutex_);
        if (result_.status.succeeded())
        {
            state_ = RunnableThreadState::Exited;
        }
    }

    void RunnableThread::publish_init(ThreadStatus status)
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            init_status_ = status;
            result_.status = std::move(status);
            state_ = result_.status.succeeded() ? RunnableThreadState::Running : RunnableThreadState::Failed;
            init_complete_ = true;
        }
        init_condition_.notify_all();
    }

    void RunnableThread::record_exception(const char* phase)
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (result_.status.succeeded())
        {
            result_.status = ThreadStatus::failure(ThreadErrorCode::UnhandledException,
                                                   std::string("unhandled exception during ") + phase);
        }
        state_ = RunnableThreadState::Failed;
    }
} // namespace toy3d
