#pragma once

#include "task_graph/graph_event.h"
#include "threading/event.h"

#include <cstdint>
#include <memory>

namespace toy3d
{
    class BaseGraphTask;

    template<typename TaskType>
    class GraphTask;

    class TaskGraphInterface
    {
    public:
        virtual ~TaskGraphInterface() = default;

        // The composition root retains ownership. These accessors only expose the
        // process-wide active scheduler during its published running lifetime.
        static bool is_running() noexcept;
        static TaskGraphInterface& get();

        virtual NamedThread get_current_thread_if_known() const = 0;
        virtual NamedThread get_render_thread() const = 0;
        virtual std::uint32_t get_num_worker_threads() const = 0;
        virtual bool is_thread_processing_tasks(NamedThread thread) const = 0;

        virtual TaskGraphStatus attach_to_thread(NamedThread current_thread) = 0;
        virtual std::uint64_t process_thread_until_idle(NamedThread current_thread) = 0;
        virtual void process_thread_until_request_return(NamedThread current_thread) = 0;
        virtual void request_return(NamedThread current_thread) = 0;

        virtual TaskWaitResult wait_until_tasks_complete(
            const GraphEventArray& tasks,
            NamedThread current_thread = NamedThread::Unknown) = 0;
        virtual void trigger_event_when_tasks_complete(
            Event& event,
            const GraphEventArray& tasks,
            NamedThread current_thread = NamedThread::Unknown) = 0;

        virtual void wake_named_thread(NamedThread thread) = 0;
        virtual TaskGraphShutdownResult shutdown(TaskGraphShutdownMode mode) = 0;

        TaskWaitResult wait_until_task_completes(
            const GraphEventRef& task,
            NamedThread current_thread = NamedThread::Unknown)
        {
            return wait_until_tasks_complete({task}, current_thread);
        }

    private:
        friend class BaseGraphTask;

        template<typename TaskType>
        friend class GraphTask;

        virtual BaseGraphTask* accept_task(std::unique_ptr<BaseGraphTask> task) = 0;
        virtual void abandon_task(BaseGraphTask& task) noexcept = 0;
        virtual void queue_task(BaseGraphTask& task) = 0;
    };
}
