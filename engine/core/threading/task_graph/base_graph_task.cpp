#include "threading/task_graph/base_graph_task.h"

#include "threading/task_graph/task_graph_interface.h"

#include <limits>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace toy3d
{
    class GraphTaskDependencyGate final
    {
      public:
        GraphTaskDependencyGate(std::uint32_t lock_count, std::function<void()> ready_callback)
            : remaining_locks_(lock_count), ready_callback_(std::move(ready_callback))
        {
        }

        void release()
        {
            const std::uint32_t previous = remaining_locks_.fetch_sub(1);
            if (previous == 0)
            {
                remaining_locks_.fetch_add(1);
                throw std::logic_error("Graph task prerequisite count underflowed");
            }
            if (previous != 1)
            {
                return;
            }

            // Cancellation takes the same mutex and cannot destroy the task while its final
            // prerequisite callback is publishing the task to the scheduler.
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (ready_callback_)
            {
                ready_callback_();
                ready_callback_ = {};
            }
        }

        void cancel() noexcept
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            ready_callback_ = {};
        }

      private:
        std::atomic<std::uint32_t> remaining_locks_{0};
        std::mutex callback_mutex_;
        std::function<void()> ready_callback_;
    };

    BaseGraphTask::BaseGraphTask(TaskGraphInterface& task_graph) : task_graph_(task_graph)
    {
    }

    BaseGraphTask::~BaseGraphTask()
    {
        if (dependency_gate_)
        {
            dependency_gate_->cancel();
        }
    }

    void BaseGraphTask::initialize_routing(NamedThread desired_thread, TaskPriority priority,
                                           SubsequentsMode subsequents_mode)
    {
        if (routing_initialized_)
        {
            throw std::logic_error("Graph task routing can only be initialized once");
        }
        if (desired_thread == NamedThread::Unknown)
        {
            throw std::invalid_argument("Graph task must select an executable thread");
        }

        desired_thread_ = desired_thread;
        priority_ = priority;
        subsequents_mode_ = subsequents_mode;
        if (subsequents_mode_ == SubsequentsMode::TrackSubsequents)
        {
            completion_event_ = GraphEvent::create_graph_event();
        }
        routing_initialized_ = true;
    }

    void BaseGraphTask::dispatch(const GraphEventArray& prerequisites, bool unlock_immediately)
    {
        if (!routing_initialized_)
        {
            throw std::logic_error("Graph task routing must be initialized before dispatch");
        }
        if (prerequisites.size() >= std::numeric_limits<std::uint32_t>::max())
        {
            throw std::overflow_error("Graph task has too many prerequisites");
        }
        for (const GraphEventRef& prerequisite : prerequisites)
        {
            if (!prerequisite)
            {
                throw std::invalid_argument("Graph task prerequisite cannot be null");
            }
        }
        if (dispatched_.exchange(true))
        {
            throw std::logic_error("Graph task can only be dispatched once");
        }

        prerequisites_ = prerequisites;
        dependency_gate_ =
            std::make_shared<GraphTaskDependencyGate>(static_cast<std::uint32_t>(prerequisites_.size()) + 1,
                                                      [this]()
                                                      {
                                                          task_graph_.queue_task(*this);
                                                      });
        const std::shared_ptr<GraphTaskDependencyGate> dependency_gate = dependency_gate_;
        for (const GraphEventRef& prerequisite : prerequisites_)
        {
            const bool registered = prerequisite->add_subsequent(
                [dependency_gate]()
                {
                    dependency_gate->release();
                });
            if (!registered)
            {
                dependency_gate->release();
            }
        }

        if (unlock_immediately)
        {
            unlock();
        }
    }

    void BaseGraphTask::unlock(NamedThread current_thread)
    {
        if (!dispatched_.load())
        {
            throw std::logic_error("Graph task must be dispatched before unlock");
        }
        if (current_thread != NamedThread::Unknown && task_graph_.get_current_thread_if_known() != current_thread)
        {
            throw TaskGraphException(TaskGraphStatus::failure(
                TaskGraphErrorCode::InvalidCaller, "GraphTask unlock caller does not own the supplied Named Thread"));
        }
        if (unlocked_.exchange(true))
        {
            throw std::logic_error("Graph task can only be unlocked once");
        }
        dependency_gate_->release();
    }

    GraphEventRef BaseGraphTask::get_completion_event() const
    {
        return completion_event_;
    }

    NamedThread BaseGraphTask::get_desired_thread() const
    {
        return desired_thread_;
    }

    TaskPriority BaseGraphTask::get_priority() const
    {
        return priority_;
    }

    void BaseGraphTask::execute(NamedThread current_thread)
    {
        if (terminal_.exchange(true))
        {
            throw std::logic_error("Graph task can only reach a terminal state once");
        }
        TaskOutcome outcome = TaskOutcome::Succeeded;
        if (completion_event_)
        {
            completion_event_->begin_task_execution();
        }
        try
        {
            execute_task(current_thread, completion_event_);
        }
        catch (...)
        {
            outcome = TaskOutcome::Failed;
        }
        if (completion_event_)
        {
            completion_event_->end_task_execution(outcome);
        }
    }

    void BaseGraphTask::cancel()
    {
        if (terminal_.exchange(true))
        {
            return;
        }
        if (dependency_gate_)
        {
            dependency_gate_->cancel();
        }
        if (completion_event_)
        {
            completion_event_->publish_completion(TaskOutcome::Cancelled);
        }
    }
} // namespace toy3d
