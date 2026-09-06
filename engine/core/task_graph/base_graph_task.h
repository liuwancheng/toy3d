#pragma once

#include "task_graph/graph_event.h"
#include "task_graph/task_graph_types.h"

#include <atomic>
#include <cstdint>

namespace toy3d
{
    class TaskGraphInterface;
    class GraphTaskDependencyGate;

    class BaseGraphTask
    {
      public:
        virtual ~BaseGraphTask();

        BaseGraphTask(const BaseGraphTask&) = delete;
        BaseGraphTask& operator=(const BaseGraphTask&) = delete;

        void unlock(NamedThread current_thread = NamedThread::Unknown);
        GraphEventRef get_completion_event() const;
        NamedThread get_desired_thread() const;
        TaskPriority get_priority() const;

        void execute(NamedThread current_thread);
        void cancel();

      protected:
        explicit BaseGraphTask(TaskGraphInterface& task_graph);

        void initialize_routing(NamedThread desired_thread, TaskPriority priority, SubsequentsMode subsequents_mode);

      private:
        template <typename TaskType> friend class GraphTask;

        void dispatch(const GraphEventArray& prerequisites, bool unlock_immediately);
        virtual void execute_task(NamedThread current_thread, const GraphEventRef& completion_event) = 0;

        TaskGraphInterface& task_graph_;
        GraphEventRef completion_event_;
        GraphEventArray prerequisites_;
        std::shared_ptr<GraphTaskDependencyGate> dependency_gate_;
        std::atomic<bool> dispatched_{false};
        std::atomic<bool> unlocked_{false};
        std::atomic<bool> terminal_{false};
        NamedThread desired_thread_ = NamedThread::Unknown;
        TaskPriority priority_ = TaskPriority::Normal;
        SubsequentsMode subsequents_mode_ = SubsequentsMode::TrackSubsequents;
        bool routing_initialized_ = false;
    };
} // namespace toy3d
