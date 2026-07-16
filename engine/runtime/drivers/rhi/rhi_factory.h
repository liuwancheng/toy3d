#pragma once

#include "drivers/rhi/rhi_device.h"

namespace toy3d
{
    // Backend selection belongs to the RHI driver layer. Engine and render
    // modules only receive the public RHIDevice abstraction.
    RHIResult<std::unique_ptr<RHIDevice>> create_default_rhi_device();
}
