#include "task_graph/stalling_task_queue.h"

#include "task_graph/base_graph_task.h"

namespace toy3d
{
    StallingTaskQueue::StallingTaskQueue(std::size_t capacity)
        : high_priority_queue_(capacity), normal_priority_queue_(capacity)
    {
    }

    bool StallingTaskQueue::enqueue(BaseGraphTask* task, TaskPriority priority)
    {
        if (task == nullptr || stopped_.load())
        {
            return false;
        }

        // Count reservation happens before queue publication so a racing consumer cannot
        // underflow the ready count after acquiring a just-published task.
        ready_count_.fetch_add(1);
        const bool enqueued = priority == TaskPriority::High
            ? high_priority_queue_.try_enqueue(task)
            : normal_priority_queue_.try_enqueue(task);
        if (!enqueued)
        {
            ready_count_.fetch_sub(1);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(sleep_mutex_);
            ++wake_generation_;
        }
        sleep_condition_.notify_one();
        return true;
    }

    bool StallingTaskQueue::try_dequeue(
        BaseGraphTask*& task,
        std::uint32_t& high_priority_streak)
    {
        task = nullptr;
        if (high_priority_streak >= maximum_high_priority_streak
            && normal_priority_queue_.try_dequeue(task))
        {
            high_priority_streak = 0;
            ready_count_.fetch_sub(1);
            return true;
        }
        if (high_priority_queue_.try_dequeue(task))
        {
            ++high_priority_streak;
            ready_count_.fetch_sub(1);
            return true;
        }
        if (normal_priority_queue_.try_dequeue(task))
        {
            high_priority_streak = 0;
            ready_count_.fetch_sub(1);
            return true;
        }
        return false;
    }

    bool StallingTaskQueue::wait_dequeue(
        BaseGraphTask*& task,
        std::uint32_t& high_priority_streak)
    {
        for (;;)
        {
            if (stopped_.load())
            {
                task = nullptr;
                return false;
            }
            if (try_dequeue(task, high_priority_streak))
            {
                return true;
            }

            std::unique_lock<std::mutex> lock(sleep_mutex_);
            const std::uint64_t observed_generation = wake_generation_;
            if (try_dequeue(task, high_priority_streak))
            {
                return true;
            }
            sleep_condition_.wait(lock, [this, observed_generation]()
            {
                return stopped_.load() || wake_generation_ != observed_generation;
            });
        }
    }

    void StallingTaskQueue::wake_all()
    {
        {
            std::lock_guard<std::mutex> lock(sleep_mutex_);
            ++wake_generation_;
        }
        sleep_condition_.notify_all();
    }

    void StallingTaskQueue::stop()
    {
        {
            std::lock_guard<std::mutex> lock(sleep_mutex_);
            stopped_.store(true);
            ++wake_generation_;
        }
        sleep_condition_.notify_all();
    }

    std::size_t StallingTaskQueue::ready_count() const
    {
        return ready_count_.load();
    }
}
