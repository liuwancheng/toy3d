#pragma once

#include "drivers/rhi/rhi_resource.h"

namespace toy3d
{
    class IWindow;

    // The platform layer owns conversion from an engine window to opaque RHI
    // surface handles. Public RHI headers never include platform window types.
    RHIResult<RHISurfaceRef> create_rhi_surface(IWindow& window);
} // namespace toy3d
