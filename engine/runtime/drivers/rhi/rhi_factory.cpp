#include "drivers/rhi/rhi_factory.h"

#if defined(TOY3D_ENABLE_VULKAN_RHI) && TOY3D_ENABLE_VULKAN_RHI
#include "drivers/vulkan/vulkan_device.h"
#endif

namespace toy3d
{
    const char* configured_rhi_backend_name()
    {
#if defined(TOY3D_ENABLE_VULKAN_RHI) && TOY3D_ENABLE_VULKAN_RHI
        return "Vulkan";
#else
        return "";
#endif
    }

    RHIResult<std::unique_ptr<RHIDevice>> create_default_rhi_device()
    {
#if defined(TOY3D_ENABLE_VULKAN_RHI) && TOY3D_ENABLE_VULKAN_RHI
        return create_vulkan_device();
#else
        return RHIResult<std::unique_ptr<RHIDevice>>::failure(
            RHIErrorCode::Unsupported, "No RHI backend is enabled. Configure with TOY3D_ENABLE_VULKAN_RHI=ON.");
#endif
    }
} // namespace toy3d
