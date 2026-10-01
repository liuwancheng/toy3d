#pragma once

#include "threading/task_graph/graph_task.h"

#include <exception>
#include <string>
#include <type_traits>
#include <utility>

namespace toy3d
{
    namespace render_command_detail
    {
        bool is_render_command_execution_allowed() noexcept;

        // Stateless bridge payload. Task Graph owns it after dispatch and destroys it on
        // the logical Rendering Thread after execution.
        template <typename Callable> class RenderCommandTask final
        {
          public:
            // C++17 invocability traits make the no-throw command contract a compile-time
            // error instead of allowing an exception to cross the render execution boundary.
            static_assert(std::is_nothrow_invocable_r<void, Callable&>::value,
                          "RenderCommand callable must have the signature void() noexcept");
            static_assert(std::is_move_constructible<Callable>::value,
                          "RenderCommand callable must be move constructible");

            RenderCommandTask(std::string command_name, Callable&& callable)
                : command_name_(std::move(command_name)), callable_(std::move(callable))
            {
            }

            NamedThread get_desired_thread() const noexcept { return NamedThread::RenderingThread; }

            TaskPriority get_priority() const noexcept { return TaskPriority::Normal; }

            SubsequentsMode get_subsequents_mode() const noexcept { return SubsequentsMode::FireAndForget; }

            void do_task(NamedThread current_thread, const GraphEventRef&) noexcept
            {
                if (current_thread != NamedThread::RenderingThread)
                {
                    std::terminate();
                }
                if (is_render_command_execution_allowed())
                {
                    callable_();
                }
            }

          private:
            std::string command_name_;
            Callable callable_;
        };

        TaskGraphInterface* get_render_command_task_graph() noexcept;
        TaskGraphStatus make_render_command_failure(TaskGraphErrorCode code, const char* reason,
                                                    const char* command_name, NamedThread current_thread);
    } // namespace render_command_detail

    // Stateless Game/Render bridge. Normal return guarantees that the callable ran
    // inline or Task Graph accepted its ownership. Contract violations throw instead
    // of exposing an admission status that ordinary fire-and-forget callers can ignore.
    template <typename Callable> void enqueue_render_command(const char* command_name, Callable&& callable)
    {
        static_assert(!std::is_lvalue_reference<Callable>::value, "enqueue_render_command requires an rvalue callable");
        // C++17 decay removes the temporary lambda's reference qualifiers so the concrete
        // closure type can be stored directly without std::function type erasure.
        static_assert(std::is_move_constructible<typename std::decay<Callable>::type>::value,
                      "RenderCommand callable must be move constructible");
        static_assert(std::is_nothrow_invocable_r<void, typename std::decay<Callable>::type&>::value,
                      "RenderCommand callable must have the signature void() noexcept");

        if (command_name == nullptr || command_name[0] == '\0')
        {
            throw TaskGraphException(render_command_detail::make_render_command_failure(
                TaskGraphErrorCode::InvalidConfig, "RenderCommand requires a non-empty command name", command_name,
                NamedThread::Unknown));
        }

        TaskGraphInterface* task_graph = render_command_detail::get_render_command_task_graph();
        if (task_graph == nullptr)
        {
            throw TaskGraphException(render_command_detail::make_render_command_failure(
                TaskGraphErrorCode::Stopped, "RenderCommand facade is not accepting commands", command_name,
                NamedThread::Unknown));
        }

        const NamedThread current_thread = task_graph->get_current_thread_if_known();
        const NamedThread logical_render_thread = task_graph->get_render_thread();
        if (current_thread == NamedThread::RenderingThread ||
            (current_thread == NamedThread::GameThread && logical_render_thread == NamedThread::GameThread))
        {
            if (render_command_detail::is_render_command_execution_allowed())
            {
                std::forward<Callable>(callable)();
            }
            return;
        }
        if (current_thread != NamedThread::GameThread)
        {
            throw TaskGraphException(render_command_detail::make_render_command_failure(
                TaskGraphErrorCode::InvalidCaller,
                "RenderCommand producer must be the Game Thread or logical Rendering Thread", command_name,
                current_thread));
        }

        try
        {
            GraphTask<render_command_detail::RenderCommandTask<typename std::decay<Callable>::type>>::create_task(
                *task_graph, nullptr, NamedThread::GameThread)
                .construct_and_dispatch_when_ready(std::string(command_name), std::forward<Callable>(callable));
        }
        catch (const TaskGraphException&)
        {
            throw;
        }
        catch (const std::exception& exception)
        {
            throw TaskGraphException(render_command_detail::make_render_command_failure(
                TaskGraphErrorCode::InvalidState, exception.what(), command_name, current_thread));
        }
        catch (...)
        {
            throw TaskGraphException(render_command_detail::make_render_command_failure(
                TaskGraphErrorCode::InvalidState, "unknown exception while dispatching RenderCommand", command_name,
                current_thread));
        }
    }
} // namespace toy3d
