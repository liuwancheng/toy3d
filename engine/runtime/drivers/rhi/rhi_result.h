#pragma once

#include <string>
#include <utility>

namespace toy3d
{
    enum class RHIErrorCode
    {
        None,
        InvalidArgument,
        Unsupported,
        OutOfMemory,
        OutOfDate,
        Suboptimal,
        DeviceLost,
        BackendFailure
    };

    class RHIStatus
    {
    public:
        RHIStatus() = default;

        static RHIStatus success()
        {
            return {};
        }

        static RHIStatus failure(RHIErrorCode code, std::string message)
        {
            return RHIStatus(code, std::move(message));
        }

        bool succeeded() const
        {
            return error_code == RHIErrorCode::None;
        }

        explicit operator bool() const
        {
            return succeeded();
        }

        RHIErrorCode code() const
        {
            return error_code;
        }

        const std::string& message() const
        {
            return error_message;
        }

    private:
        RHIStatus(RHIErrorCode code, std::string message)
            : error_code(code)
            , error_message(std::move(message))
        {
        }

        RHIErrorCode error_code = RHIErrorCode::None;
        std::string error_message;
    };

    template<typename T>
    class RHIResult
    {
    public:
        static RHIResult success(T value)
        {
            return RHIResult(std::move(value));
        }

        static RHIResult failure(RHIErrorCode code, std::string message)
        {
            return RHIResult(RHIStatus::failure(code, std::move(message)));
        }

        bool succeeded() const
        {
            return result_status.succeeded();
        }

        explicit operator bool() const
        {
            return succeeded();
        }

        const RHIStatus& status() const
        {
            return result_status;
        }

        const T& value() const&
        {
            return result_value;
        }

        T& value() &
        {
            return result_value;
        }

        T&& value() &&
        {
            return std::move(result_value);
        }

    private:
        explicit RHIResult(T value)
            : result_value(std::move(value))
        {
        }

        explicit RHIResult(RHIStatus status)
            : result_status(std::move(status))
        {
        }

        RHIStatus result_status;
        T result_value{};
    };
}
