#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    class RHICommandContext;
    class RHIDevice;

    // Render-side Shader ABI helper. Upload and access transitions are recorded
    // into the caller-owned command context; this function never submits.
    RHIResult<RHIBufferRef> create_uploaded_shader_uniform_buffer(
        RHIDevice& device,
        RHICommandContext& context,
        const std::vector<std::uint8_t>& bytes,
        const std::string& debug_name);
}
