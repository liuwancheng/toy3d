#pragma once

#include "task_graph/task_graph_interface.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace toy3d
{
    class ThreadManager;

    struct TaskGraphConfig
    {
        // Zero delegates worker-count selection to the composition-root policy below.
        std::uint32_t worker_thread_count = 0;
        std::uint32_t max_tasks_in_flight = 4096;
        bool multithreaded = true;
    };

    using TaskGraphDiagnostics = std::function<void(const TaskGraphStatus&)>;

    class TaskGraphCreateResult final
    {
    public:
        TaskGraphCreateResult(
            TaskGraphStatus status,
            std::unique_ptr<TaskGraphInterface> task_graph);

        bool succeeded() const;
        const TaskGraphStatus& status() const;
        std::unique_ptr<TaskGraphInterface> take_task_graph();

    private:
        TaskGraphStatus status_;
        std::unique_ptr<TaskGraphInterface> task_graph_;
    };

    TaskGraphCreateResult create_task_graph(
        TaskGraphConfig config,
        ThreadManager& thread_manager,
        TaskGraphDiagnostics diagnostics = {});
}
