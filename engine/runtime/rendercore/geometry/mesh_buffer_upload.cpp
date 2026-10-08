#include "rendercore/geometry/mesh_buffer_upload.h"

#include <utility>

#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_command_context.h"

namespace toy3d
{
    RHIStatus record_mesh_buffer_upload(RHIDevice& device, RHIGraphicsCommandContext& context, const void* initial_data,
                                        std::size_t initial_data_size, RHIResourceUsage usage, RHIAccess final_access,
                                        const char* debug_name, RHIBufferRef& out_buffer,
                                        bool& out_deterministic_failure)
    {
        out_deterministic_failure = false;
        if (initial_data == nullptr || initial_data_size == 0u)
        {
            out_deterministic_failure = true;
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Mesh buffer upload requires a non-empty initial payload");
        }

        RHIBufferDesc desc;
        desc.size = static_cast<std::uint64_t>(initial_data_size);
        desc.usage = usage | RHIResourceUsage::CopyDestination;
        desc.initial_access = RHIAccess::Common;
        desc.debug_name = debug_name;
        RHIResult<RHIBufferRef> created = device.create_buffer(desc);
        if (!created)
        {
            out_deterministic_failure = created.status().code() == RHIErrorCode::InvalidArgument ||
                                        created.status().code() == RHIErrorCode::Unsupported;
            return created.status();
        }
        RHIBufferRef candidate = std::move(created).value();

        RHIResourceTransition to_copy;
        to_copy.resource = candidate;
        to_copy.before = RHIAccess::Common;
        to_copy.after = RHIAccess::CopyDestination;
        RHIStatus status = context.transition_resources({to_copy});
        if (!status)
        {
            out_deterministic_failure =
                status.code() == RHIErrorCode::InvalidArgument || status.code() == RHIErrorCode::Unsupported;
            return status;
        }

        RHIBufferUploadDesc upload;
        upload.destination = candidate;
        upload.source.data = initial_data;
        upload.source.size = initial_data_size;
        status = context.upload_buffer(upload);
        if (!status)
        {
            out_deterministic_failure =
                status.code() == RHIErrorCode::InvalidArgument || status.code() == RHIErrorCode::Unsupported;
            return status;
        }

        RHIResourceTransition to_final;
        to_final.resource = candidate;
        to_final.before = RHIAccess::CopyDestination;
        to_final.after = final_access;
        status = context.transition_resources({to_final});
        if (!status)
        {
            out_deterministic_failure =
                status.code() == RHIErrorCode::InvalidArgument || status.code() == RHIErrorCode::Unsupported;
            return status;
        }

        out_buffer = std::move(candidate);
        return RHIStatus::success();
    }

} // namespace toy3d
