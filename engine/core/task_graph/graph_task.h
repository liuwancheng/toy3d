#pragma once

#include "task_graph/base_graph_task.h"
#include "task_graph/task_graph_interface.h"

#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace toy3d
{
    template <typename TaskType> class GraphTask final : public BaseGraphTask
    {
      public:
        class Constructor final
        {
          public:
            template <typename... Args> GraphEventRef construct_and_dispatch_when_ready(Args&&... args)
            {
                validate_prerequisites();
                std::unique_ptr<GraphTask> task = std::make_unique<GraphTask>(task_graph_, std::forward<Args>(args)...);
                GraphTask* accepted = static_cast<GraphTask*>(task_graph_.accept_task(std::move(task)));
                GraphEventRef completion_event = accepted->get_completion_event();
                try
                {
                    accepted->dispatch(prerequisites_, true);
                }
                catch (...)
                {
                    task_graph_.abandon_task(*accepted);
                    throw;
                }
                return completion_event;
            }

            template <typename... Args> GraphTask* construct_and_hold(Args&&... args)
            {
                validate_prerequisites();
                std::unique_ptr<GraphTask> task = std::make_unique<GraphTask>(task_graph_, std::forward<Args>(args)...);
                GraphTask* accepted = static_cast<GraphTask*>(task_graph_.accept_task(std::move(task)));
                try
                {
                    accepted->dispatch(prerequisites_, false);
                }
                catch (...)
                {
                    task_graph_.abandon_task(*accepted);
                    throw;
                }
                return accepted;
            }

          private:
            friend class GraphTask;

            void validate_prerequisites() const
            {
                if (prerequisites_.size() >= std::numeric_limits<std::uint32_t>::max())
                {
                    throw TaskGraphException(TaskGraphStatus::failure(TaskGraphErrorCode::InvalidPrerequisite,
                                                                      "Graph task has too many prerequisites"));
                }
                for (const GraphEventRef& prerequisite : prerequisites_)
                {
                    if (!prerequisite)
                    {
                        throw TaskGraphException(TaskGraphStatus::failure(TaskGraphErrorCode::InvalidPrerequisite,
                                                                          "Graph task prerequisite cannot be null"));
                    }
                }
            }

            Constructor(TaskGraphInterface& task_graph, GraphEventArray prerequisites, NamedThread current_thread)
                : task_graph_(task_graph), prerequisites_(std::move(prerequisites))
            {
                if (current_thread != NamedThread::Unknown &&
                    task_graph_.get_current_thread_if_known() != current_thread)
                {
                    throw TaskGraphException(TaskGraphStatus::failure(
                        TaskGraphErrorCode::InvalidCaller,
                        "GraphTask create_task caller does not own the supplied Named Thread"));
                }
            }

            TaskGraphInterface& task_graph_;
            GraphEventArray prerequisites_;
        };

        template <typename... Args>
        explicit GraphTask(TaskGraphInterface& task_graph, Args&&... args)
            : BaseGraphTask(task_graph), task_(std::forward<Args>(args)...)
        {
            initialize_routing(task_.get_desired_thread(), task_.get_priority(), task_.get_subsequents_mode());
        }

        static Constructor create_task(TaskGraphInterface& task_graph, const GraphEventArray* prerequisites = nullptr,
                                       NamedThread current_thread = NamedThread::Unknown)
        {
            return Constructor(task_graph, prerequisites != nullptr ? *prerequisites : GraphEventArray{},
                               current_thread);
        }

      private:
        void execute_task(NamedThread current_thread, const GraphEventRef& completion_event) override
        {
            task_.do_task(current_thread, completion_event);
        }

        TaskType task_;
    };

    using GraphTaskFunction = std::function<void(NamedThread current_thread, const GraphEventRef& completion_event)>;

    GraphEventRef dispatch_graph_task(TaskGraphInterface& task_graph, std::string debug_name,
                                      GraphTaskFunction function, NamedThread desired_thread = NamedThread::AnyWorker,
                                      const GraphEventArray* prerequisites = nullptr,
                                      SubsequentsMode mode = SubsequentsMode::TrackSubsequents);
} // namespace toy3d
