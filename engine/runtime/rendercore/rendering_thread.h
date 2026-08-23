#pragma once

#include "threading/threading_types.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace toy3d
{
    class TaskGraphInterface;
    class Thread;
    class ThreadManager;

    enum class RenderingThreadMode
    {
        MultiThread,
        SingleThread
    };

    // Render-side lifecycle controller. Engine owns and calls it from the Game Thread;
    // the mutable renderer bootstrap, named-queue pump, and teardown run on the logical
    // Rendering Thread selected at construction. It owns an OS thread only in MultiThread mode.
    class RenderingThread final
    {
    public:
        RenderingThread(
            ThreadManager& thread_manager,
            TaskGraphInterface& task_graph,
            RenderingThreadMode mode,
            std::function<std::unique_ptr<Thread>(std::function<void()>)> thread_factory = {});
        ~RenderingThread();

        RenderingThread(const RenderingThread&) = delete;
        RenderingThread& operator=(const RenderingThread&) = delete;

        ThreadStatus start(const std::function<ThreadStatus()>& bootstrap = {});
        ThreadStatus stop(const std::function<ThreadStatus()>& teardown = {});

        bool is_ready() const noexcept;
        RenderingThreadMode get_mode() const noexcept;
        std::thread::id get_thread_id() const noexcept;

    private:
        ThreadStatus run_callback(
            const std::function<ThreadStatus()>& callback,
            const char* phase) const noexcept;

        ThreadManager& thread_manager_;
        TaskGraphInterface& task_graph_;
        const RenderingThreadMode mode_;
        std::function<std::unique_ptr<Thread>(std::function<void()>)> thread_factory_;
        std::unique_ptr<Thread> thread_;
        std::atomic<bool> ready_{false};
        bool started_ = false;
    };
}
