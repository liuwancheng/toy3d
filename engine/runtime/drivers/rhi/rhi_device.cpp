#include "drivers/rhi/rhi_device.h"

namespace toy3d
{
    RHIStatus validate_device_desc(const RHIDeviceDesc& desc)
    {
        if (!desc.primary_surface)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "First-phase devices require a primary presentation surface.");
        }
        return validate_surface_desc(desc.primary_surface->desc());
    }
}
