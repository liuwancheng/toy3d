#include "rendercore/render_command.h"

#include "rendercore/render_command_internal.h"

#include <atomic>
#include <string>
#include <utility>

namespace toy3d
{
    namespace render_command_detail
    {
        namespace
        {
            std::atomic<TaskGraphInterface*> enabled_task_graph{nullptr};
            std::atomic<bool> execution_allowed{false};

            const char* get_thread_name(NamedThread thread) noexcept
            {
                switch (thread)
                {
                case NamedThread::GameThread:
                    return "GameThread";
                case NamedThread::RenderingThread:
                    return "RenderingThread";
                case NamedThread::AnyWorker:
                    return "AnyWorker";
                case NamedThread::Unknown:
                default:
                    return "Unknown";
                }
            }
        }

        TaskGraphStatus enable_render_command_facade(
            TaskGraphInterface& task_graph) noexcept
        {
            TaskGraphInterface* expected = nullptr;
            if (enabled_task_graph.compare_exchange_strong(expected, &task_graph)
                || expected == &task_graph)
            {
                execution_allowed.store(true);
                return TaskGraphStatus::success();
            }
            return TaskGraphStatus::failure(
                TaskGraphErrorCode::InvalidState,
                "RenderCommand facade is already bound to another Task Graph");
        }

        void disable_render_command_execution(
            TaskGraphInterface& task_graph) noexcept
        {
            if (enabled_task_graph.load() == &task_graph)
            {
                execution_allowed.store(false);
            }
        }

        void disable_render_command_facade(TaskGraphInterface& task_graph) noexcept
        {
            TaskGraphInterface* expected = &task_graph;
            enabled_task_graph.compare_exchange_strong(expected, nullptr);
        }

        TaskGraphInterface* get_render_command_task_graph() noexcept
        {
            return enabled_task_graph.load();
        }

        bool is_render_command_execution_allowed() noexcept
        {
            return execution_allowed.load();
        }

        TaskGraphStatus make_render_command_failure(
            TaskGraphErrorCode code,
            const char* reason,
            const char* command_name,
            NamedThread current_thread)
        {
            std::string message = reason != nullptr ? reason : "RenderCommand failure";
            message += "; command=";
            message += command_name != nullptr ? command_name : "<null>";
            message += "; producer=";
            message += get_thread_name(current_thread);
            return TaskGraphStatus::failure(code, std::move(message));
        }
    }
}
