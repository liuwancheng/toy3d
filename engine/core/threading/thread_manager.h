#pragma once

#include "threading/threading_types.h"

#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace toy3d
{
    class RunnableThread;

    using ThreadDiagnosticsSink = std::function<void(const ThreadStatus&)>;

    struct ThreadInfo
    {
        std::thread::id id;
        std::string name;
        RunnableThreadState state = RunnableThreadState::Created;
    };

    class ThreadManager final
    {
      public:
        explicit ThreadManager(ThreadDiagnosticsSink diagnostics_sink = {});

        ThreadInfo get_thread(std::thread::id id) const;
        std::string get_thread_name(std::thread::id id) const;
        void for_each_thread(const std::function<void(const ThreadInfo&)>& function) const;

      private:
        friend class RunnableThread;

        void add_thread(RunnableThread& thread);
        void remove_thread(RunnableThread& thread);
        void report(const ThreadStatus& status) const noexcept;

        mutable std::mutex mutex_;
        std::unordered_map<std::thread::id, RunnableThread*> threads_;
        ThreadDiagnosticsSink diagnostics_sink_;
    };
} // namespace toy3d
