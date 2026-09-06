#pragma once

#include "threading/runnable.h"
#include "threading/threading_types.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace toy3d
{
    class ThreadManager;

    struct RunnableThreadConfig
    {
        std::string name;
    };

    class RunnableThread;

    class RunnableThreadCreateResult final
    {
      public:
        bool succeeded() const;
        const ThreadStatus& status() const;
        std::unique_ptr<RunnableThread> take_thread();

      private:
        friend class RunnableThread;

        RunnableThreadCreateResult(ThreadStatus status, std::unique_ptr<RunnableThread> thread);

        ThreadStatus status_;
        std::unique_ptr<RunnableThread> thread_;
    };

    class RunnableThread final
    {
      public:
        static RunnableThreadCreateResult create(ThreadManager& thread_manager, std::unique_ptr<Runnable> runnable,
                                                 RunnableThreadConfig config);

        // Public only so std::make_unique can build the factory-owned object without raw new.
        // Callers use create(), which performs and reports the init handshake.
        RunnableThread(ThreadManager& thread_manager, std::unique_ptr<Runnable> runnable, RunnableThreadConfig config);
        ~RunnableThread();

        RunnableThread(const RunnableThread&) = delete;
        RunnableThread& operator=(const RunnableThread&) = delete;

        void request_stop();
        ThreadStatus wait_for_completion();

        bool joinable() const;
        std::thread::id get_thread_id() const;
        const std::string& get_thread_name() const;
        RunnableThreadState get_state() const;
        ThreadExecutionResult get_result() const;

      private:
        void thread_entry();
        void publish_init(ThreadStatus status);
        void record_exception(const char* phase);

        ThreadManager& thread_manager_;
        std::unique_ptr<Runnable> runnable_;
        std::string name_;
        std::thread thread_;
        std::thread::id thread_id_;

        mutable std::mutex state_mutex_;
        std::condition_variable init_condition_;
        bool init_complete_ = false;
        bool stop_requested_ = false;
        bool registered_ = false;
        RunnableThreadState state_ = RunnableThreadState::Created;
        ThreadStatus init_status_;
        ThreadExecutionResult result_;

        mutable std::mutex join_mutex_;
    };
} // namespace toy3d
