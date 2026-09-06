#include "drivers/rhi/rhi_result.h"

#include "logging/logger.h"

namespace toy3d
{
    namespace
    {
        const char* error_code_name(RHIErrorCode code)
        {
            switch (code)
            {
            case RHIErrorCode::None:
                return "None";
            case RHIErrorCode::InvalidArgument:
                return "InvalidArgument";
            case RHIErrorCode::Unsupported:
                return "Unsupported";
            case RHIErrorCode::OutOfMemory:
                return "OutOfMemory";
            case RHIErrorCode::NotReady:
                return "NotReady";
            case RHIErrorCode::OutOfDate:
                return "OutOfDate";
            case RHIErrorCode::Suboptimal:
                return "Suboptimal";
            case RHIErrorCode::DeviceLost:
                return "DeviceLost";
            case RHIErrorCode::BackendFailure:
                return "BackendFailure";
            }
            return "Unknown";
        }
    } // namespace

    RHIStatus RHIStatus::failure(RHIErrorCode code, std::string message)
    {
        if (code == RHIErrorCode::NotReady)
        {
            TOY_LOG_DEBUG("RHI recoverable status [{}]: {}", error_code_name(code), message);
        }
        else if (code == RHIErrorCode::OutOfDate || code == RHIErrorCode::Suboptimal)
        {
            TOY_LOG_INFO("RHI recoverable status [{}]: {}", error_code_name(code), message);
        }
        else
        {
            TOY_LOG_ERROR("RHI failure [{}]: {}", error_code_name(code), message);
        }
        return RHIStatus(code, std::move(message));
    }
} // namespace toy3d
