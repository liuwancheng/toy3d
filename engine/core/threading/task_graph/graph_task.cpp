#include "threading/task_graph/graph_task.h"

#include <utility>

namespace toy3d
{
    namespace
    {
        class FunctionGraphTask final
        {
          public:
            FunctionGraphTask(std::string debug_name, GraphTaskFunction function, NamedThread desired_thread,
                              SubsequentsMode mode)
                : debug_name_(std::move(debug_name)), function_(std::move(function)), desired_thread_(desired_thread),
                  mode_(mode)
            {
            }

            NamedThread get_desired_thread() const
            {
                return desired_thread_;
            }

            TaskPriority get_priority() const
            {
                return TaskPriority::Normal;
            }

            SubsequentsMode get_subsequents_mode() const
            {
                return mode_;
            }

            void do_task(NamedThread current_thread, const GraphEventRef& completion_event)
            {
                function_(current_thread, completion_event);
            }

          private:
            std::string debug_name_;
            GraphTaskFunction function_;
            NamedThread desired_thread_ = NamedThread::AnyWorker;
            SubsequentsMode mode_ = SubsequentsMode::TrackSubsequents;
        };
    } // namespace

    GraphEventRef dispatch_graph_task(TaskGraphInterface& task_graph, std::string debug_name,
                                      GraphTaskFunction function, NamedThread desired_thread,
                                      const GraphEventArray* prerequisites, SubsequentsMode mode)
    {
        return GraphTask<FunctionGraphTask>::create_task(task_graph, prerequisites)
            .construct_and_dispatch_when_ready(std::move(debug_name), std::move(function), desired_thread, mode);
    }
} // namespace toy3d
