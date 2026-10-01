#include "drivers/vulkan/vulkan_device.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "platform/platform_defines.h"

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
} // namespace

int main()
{
    using namespace toy3d;

    RHISurfaceDesc surface_desc;
#if WITH_WIN
    surface_desc.platform = RHISurfacePlatform::MacOS;
#elif WITH_MAC
    surface_desc.platform = RHISurfacePlatform::Win32;
    surface_desc.application_handle = reinterpret_cast<void*>(std::uintptr_t{1});
#endif
    surface_desc.window_handle = reinterpret_cast<void*>(std::uintptr_t{1});
    surface_desc.debug_name = "Unsupported lifecycle test surface";

    RHIDeviceDesc device_desc;
    device_desc.primary_surface = std::make_shared<RHISurface>(surface_desc);
    device_desc.debug_name = "Vulkan partial initialization lifecycle test";

    VulkanDevice device;
    const RHIStatus first_initialize = device.initialize(device_desc);
    require(!first_initialize && first_initialize.code() == RHIErrorCode::Unsupported,
            "A platform-mismatched surface must fail after Vulkan instance setup.");

    RHIBufferDesc buffer_desc;
    buffer_desc.size = 16;
    buffer_desc.usage = RHIResourceUsage::VertexBuffer;
    const auto rejected_creation = device.create_buffer(buffer_desc);
    require(!rejected_creation && rejected_creation.status().code() == RHIErrorCode::NotReady,
            "A partially initialized Vulkan device must return to the uninitialized frontend state.");

    const RHIStatus second_initialize = device.initialize(device_desc);
    require(!second_initialize && second_initialize.code() == RHIErrorCode::Unsupported,
            "Vulkan partial-initialization cleanup must permit a second initialization attempt.");
    require(static_cast<bool>(device.shutdown()),
            "Vulkan shutdown must remain idempotent after partial-initialization cleanup.");
    return EXIT_SUCCESS;
}
