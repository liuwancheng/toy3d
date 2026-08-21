#pragma once

#include "task_graph/task_graph_types.h"
#include "threading/containers/bounded_mpmc_queue.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace toy3d
{
    class BaseGraphTask;

    class StallingTaskQueue final
    {
    public:
        explicit StallingTaskQueue(std::size_t capacity);

        StallingTaskQueue(const StallingTaskQueue&) = delete;
        StallingTaskQueue& operator=(const StallingTaskQueue&) = delete;

        bool enqueue(BaseGraphTask* task, TaskPriority priority);
        bool try_dequeue(BaseGraphTask*& task, std::uint32_t& high_priority_streak);
        bool wait_dequeue(BaseGraphTask*& task, std::uint32_t& high_priority_streak);

        void wake_all();
        void stop();
        std::size_t ready_count() const;

    private:
        static constexpr std::uint32_t maximum_high_priority_streak = 8;

        detail::BoundedMpmcQueue<BaseGraphTask*> high_priority_queue_;
        detail::BoundedMpmcQueue<BaseGraphTask*> normal_priority_queue_;
        std::atomic<std::size_t> ready_count_{0};
        std::atomic<bool> stopped_{false};
        std::mutex sleep_mutex_;
        std::condition_variable sleep_condition_;
        std::uint64_t wake_generation_ = 0;
    };
}
