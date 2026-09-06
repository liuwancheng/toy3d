#pragma once

#include "drivers/rhi/rhi_device.h"

namespace toy3d
{
    // Backend selection belongs to the RHI driver layer. Engine and render
    // modules only receive the public RHIDevice abstraction.
    const char* configured_rhi_backend_name();
    RHIResult<std::unique_ptr<RHIDevice>> create_default_rhi_device();
} // namespace toy3d
