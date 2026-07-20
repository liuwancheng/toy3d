#include "rhi_resource.h"

namespace toy3d
{
    // Public RHI resources are intentionally data-only identities. Native
    // destruction is owned by backend subclasses and deferred by queue completion value.

    RHIStatus validate_surface_desc(const RHISurfaceDesc& desc)
    {
        if (desc.platform == RHISurfacePlatform::Unknown || desc.window_handle == nullptr)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Surface requires a supported platform and window handle.");
        }
        if (desc.platform == RHISurfacePlatform::Win32 && desc.application_handle == nullptr)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Win32 surface requires an application handle.");
        }
        return RHIStatus::success();
    }
}
