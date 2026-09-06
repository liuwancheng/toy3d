#include "threading/thread_manager.h"

#include "threading/runnable_thread.h"

#include <vector>
#include <utility>

namespace toy3d
{
    ThreadManager::ThreadManager(ThreadDiagnosticsSink diagnostics_sink)
        : diagnostics_sink_(std::move(diagnostics_sink))
    {
    }

    ThreadInfo ThreadManager::get_thread(std::thread::id id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = threads_.find(id);
        if (found == threads_.end())
        {
            return {};
        }
        return {id, found->second->get_thread_name(), found->second->get_state()};
    }

    std::string ThreadManager::get_thread_name(std::thread::id id) const
    {
        return get_thread(id).name;
    }

    void ThreadManager::for_each_thread(const std::function<void(const ThreadInfo&)>& function) const
    {
        std::vector<ThreadInfo> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot.reserve(threads_.size());
            for (const auto& entry : threads_)
            {
                snapshot.push_back({entry.first, entry.second->get_thread_name(), entry.second->get_state()});
            }
        }

        // Callbacks run without the registry mutex so diagnostics cannot deadlock registration.
        for (const ThreadInfo& info : snapshot)
        {
            function(info);
        }
    }

    void ThreadManager::add_thread(RunnableThread& thread)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        threads_[thread.get_thread_id()] = &thread;
    }

    void ThreadManager::remove_thread(RunnableThread& thread)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = threads_.find(thread.get_thread_id());
        if (found != threads_.end() && found->second == &thread)
        {
            threads_.erase(found);
        }
    }

    void ThreadManager::report(const ThreadStatus& status) const noexcept
    {
        try
        {
            if (diagnostics_sink_)
            {
                diagnostics_sink_(status);
            }
        }
        catch (...)
        {
            // Diagnostics must never make thread cleanup fail or terminate the process.
        }
    }
} // namespace toy3d
