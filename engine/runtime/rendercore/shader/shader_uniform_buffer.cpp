#include "rendercore/shader/shader_uniform_buffer.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"

namespace toy3d
{
    RHIResult<RHIBufferRef> create_uploaded_shader_uniform_buffer(
        RHIDevice& device,
        RHICommandContext& context,
        const std::vector<std::uint8_t>& bytes,
        const std::string& debug_name)
    {
        RHIBufferDesc buffer_desc;
        buffer_desc.size = bytes.size();
        buffer_desc.usage = rhi_enum_or(
            RHIResourceUsage::UniformBuffer,
            RHIResourceUsage::CopyDestination);
        buffer_desc.initial_access = RHIAccess::Common;
        buffer_desc.debug_name = debug_name;
        RHIResult<RHIBufferRef> buffer = device.create_buffer(buffer_desc);
        if (!buffer)
        {
            return buffer;
        }

        RHIResourceTransition to_copy;
        to_copy.resource = buffer.value();
        to_copy.before = RHIAccess::Common;
        to_copy.after = RHIAccess::CopyDestination;
        RHIStatus status = context.transition_resources({to_copy});
        if (!status)
        {
            return RHIResult<RHIBufferRef>::failure(
                status.code(), status.message());
        }

        RHIBufferUploadDesc upload;
        upload.destination = buffer.value();
        upload.source.data = bytes.data();
        upload.source.size = bytes.size();
        upload.source.consumption =
            RHIInitialData::Consumption::CopiedBeforeReturn;
        status = context.upload_buffer(upload);
        if (!status)
        {
            return RHIResult<RHIBufferRef>::failure(
                status.code(), status.message());
        }

        RHIResourceTransition to_uniform;
        to_uniform.resource = buffer.value();
        to_uniform.before = RHIAccess::CopyDestination;
        to_uniform.after = RHIAccess::UniformBuffer;
        status = context.transition_resources({to_uniform});
        if (!status)
        {
            return RHIResult<RHIBufferRef>::failure(
                status.code(), status.message());
        }
        return buffer;
    }
}
