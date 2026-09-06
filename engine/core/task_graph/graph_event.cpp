#include "task_graph/graph_event.h"

#include "task_graph/task_graph_interface.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace toy3d
{
    GraphEventRef GraphEvent::create_graph_event()
    {
        return std::make_shared<GraphEvent>();
    }

    bool GraphEvent::is_complete() const
    {
        // Acquire pairs with terminal publication so payload writes are visible to waiters.
        return complete_.load(std::memory_order_acquire);
    }

    TaskOutcome GraphEvent::get_outcome() const
    {
        // The acquire observes both the terminal outcome and writes performed by the task.
        if (!complete_.load(std::memory_order_acquire))
        {
            return TaskOutcome::Pending;
        }
        return outcome_.load(std::memory_order_relaxed);
    }

    void GraphEvent::wait(TaskGraphInterface& task_graph, NamedThread current_thread) const
    {
        GraphEventRef self = std::const_pointer_cast<GraphEvent>(shared_from_this());
        task_graph.wait_until_tasks_complete({std::move(self)}, current_thread);
    }

    bool GraphEvent::add_subsequent(std::function<void()> subsequent)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_)
        {
            return false;
        }
        subsequents_.push_back(std::move(subsequent));
        return true;
    }

    void GraphEvent::begin_task_execution()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || accepting_completion_dependencies_)
        {
            throw std::logic_error("Graph event entered an invalid execution state");
        }
        accepting_completion_dependencies_ = true;
    }

    void GraphEvent::dont_complete_until(GraphEventRef event)
    {
        if (!event)
        {
            throw std::invalid_argument("Completion dependency cannot be null");
        }
        if (event.get() == this)
        {
            throw std::invalid_argument("Graph event cannot wait for itself");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_completion_dependencies_)
        {
            throw std::logic_error("dont_complete_until is only valid while the owning task executes");
        }
        completion_dependencies_.push_back(std::move(event));
    }

    void GraphEvent::end_task_execution(TaskOutcome outcome)
    {
        GraphEventArray dependencies;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accepting_completion_dependencies_)
            {
                throw std::logic_error("Graph event is not executing an owning task");
            }
            accepting_completion_dependencies_ = false;
            dependencies.swap(completion_dependencies_);
        }

        if (dependencies.empty())
        {
            publish_completion(outcome);
            return;
        }

        auto remaining = std::make_shared<std::atomic<std::size_t>>(dependencies.size());
        GraphEventRef target = shared_from_this();
        auto dependency_completed = [target, remaining, outcome]()
        {
            if (remaining->fetch_sub(1) == 1)
            {
                target->publish_completion(outcome);
            }
        };
        for (const GraphEventRef& dependency : dependencies)
        {
            if (!dependency->add_subsequent(dependency_completed))
            {
                dependency_completed();
            }
        }
    }

    void GraphEvent::publish_completion(TaskOutcome outcome)
    {
        std::vector<std::function<void()>> subsequents;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_)
            {
                throw std::logic_error("Graph event completion can only be published once");
            }
            closed_ = true;
            outcome_.store(outcome, std::memory_order_relaxed);
            // Release publishes the outcome and every write sequenced before task completion.
            complete_.store(true, std::memory_order_release);
            subsequents.swap(subsequents_);
        }

        std::exception_ptr first_failure;
        for (const std::function<void()>& subsequent : subsequents)
        {
            try
            {
                subsequent();
            }
            catch (...)
            {
                if (!first_failure)
                {
                    first_failure = std::current_exception();
                }
            }
        }
        if (first_failure)
        {
            // One failed scheduler callback must not prevent unrelated dependents from being
            // released. The executor boundary still receives the first diagnostic failure.
            std::rethrow_exception(first_failure);
        }
    }
} // namespace toy3d
