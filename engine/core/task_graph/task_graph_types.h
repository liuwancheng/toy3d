#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace toy3d
{
    enum class NamedThread
    {
        Unknown,
        GameThread,
        RenderingThread,
        AnyWorker
    };

    enum class TaskPriority
    {
        Normal,
        High
    };

    enum class SubsequentsMode
    {
        TrackSubsequents,
        FireAndForget
    };

    enum class TaskOutcome
    {
        Pending,
        Succeeded,
        Failed,
        Cancelled
    };

    enum class TaskGraphErrorCode
    {
        None,
        InvalidConfig,
        InvalidState,
        InvalidCaller,
        InvalidGraphEvent,
        InvalidPrerequisite,
        TargetUnavailable,
        Overloaded,
        Stopped,
        Timeout,
        Cancelled,
        TaskFailed,
        DeadlockRisk,
        ThreadCreateFailed
    };

    struct TaskGraphStatus
    {
        TaskGraphErrorCode code = TaskGraphErrorCode::None;
        std::string message;

        bool succeeded() const { return code == TaskGraphErrorCode::None; }

        static TaskGraphStatus success() { return {}; }

        static TaskGraphStatus failure(TaskGraphErrorCode error_code, std::string error_message)
        {
            return {error_code, std::move(error_message)};
        }
    };

    struct TaskWaitResult
    {
        TaskGraphStatus status;

        bool succeeded() const { return status.succeeded(); }
    };

    enum class TaskGraphShutdownMode
    {
        Drain,
        CancelPending
    };

    struct TaskGraphShutdownResult
    {
        TaskGraphStatus status;

        bool succeeded() const { return status.succeeded(); }
    };

    class TaskGraphException final : public std::runtime_error
    {
      public:
        explicit TaskGraphException(TaskGraphStatus status)
            : std::runtime_error(status.message), status_(std::move(status))
        {
        }

        const TaskGraphStatus& status() const { return status_; }

      private:
        TaskGraphStatus status_;
    };
} // namespace toy3d
