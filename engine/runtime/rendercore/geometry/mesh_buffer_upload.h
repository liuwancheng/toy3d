#pragma once

#include "drivers/rhi/rhi_resource.h"

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;

    // Creates a fresh candidate and records owned upload bytes before draw passes.
    // The resource owner decides commit/discard through RenderResourceManager.
    RHIStatus record_mesh_buffer_upload(RHIDevice& device, RHIGraphicsCommandContext& context, const void* initial_data,
                                        std::size_t initial_data_size, RHIResourceUsage usage, RHIAccess final_access,
                                        const char* debug_name, RHIBufferRef& out_buffer,
                                        bool& out_deterministic_failure);
} // namespace toy3d
