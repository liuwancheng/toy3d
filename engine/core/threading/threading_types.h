#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace toy3d
{
    enum class ThreadErrorCode
    {
        None,
        InvalidState,
        InvalidConfig,
        CreateFailed,
        InitFailed,
        InvalidCaller,
        UnhandledException,
        NotJoined
    };

    struct ThreadStatus
    {
        ThreadErrorCode code = ThreadErrorCode::None;
        std::string message;

        bool succeeded() const
        {
            return code == ThreadErrorCode::None;
        }

        static ThreadStatus success()
        {
            return {};
        }

        static ThreadStatus failure(ThreadErrorCode error_code, std::string error_message)
        {
            return {error_code, std::move(error_message)};
        }
    };

    enum class RunnableThreadState
    {
        Created,
        Initializing,
        Running,
        StopRequested,
        Exited,
        Joined,
        Failed
    };

    struct ThreadExecutionResult
    {
        ThreadStatus status;
        std::uint32_t return_code = 0;
    };
}
