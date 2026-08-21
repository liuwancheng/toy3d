#pragma once

#include "task_graph/task_graph_types.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace toy3d
{
    class BaseGraphTask;
    class TaskGraph;
    class TaskGraphInterface;
    class GraphEvent;

    using GraphEventRef = std::shared_ptr<GraphEvent>;
    using GraphEventArray = std::vector<GraphEventRef>;

    class GraphEvent final : public std::enable_shared_from_this<GraphEvent>
    {
    public:
        static GraphEventRef create_graph_event();

        bool is_complete() const;
        TaskOutcome get_outcome() const;
        void wait(
            TaskGraphInterface& task_graph,
            NamedThread current_thread = NamedThread::Unknown) const;

        void dont_complete_until(GraphEventRef event);

    private:
        friend class BaseGraphTask;
        friend class TaskGraph;

        bool add_subsequent(std::function<void()> subsequent);
        void begin_task_execution();
        void end_task_execution(TaskOutcome outcome);
        void publish_completion(TaskOutcome outcome);

        std::atomic<bool> complete_{false};
        std::atomic<TaskOutcome> outcome_{TaskOutcome::Pending};
        mutable std::mutex mutex_;
        bool closed_ = false;
        bool accepting_completion_dependencies_ = false;
        std::vector<std::function<void()>> subsequents_;
        GraphEventArray completion_dependencies_;
    };
}
