#include "platform/rhi_surface_factory.h"

#include "platform/window_interface.h"

#if WITH_WIN64
#include "platform/win/win32_window.h"
#elif WITH_MAC
#include "platform/mac/mac_window.h"
#elif WITH_ANDROID
#include "platform/android/android_window.h"
#endif

#include <memory>

namespace toy3d
{
    RHIResult<RHISurfaceRef> create_rhi_surface(IWindow& window)
    {
        RHISurfaceDesc desc;
        desc.debug_name = "MainWindowSurface";

#if WITH_WIN64
        auto* win32_window = dynamic_cast<Win32Window*>(&window);
        if (win32_window == nullptr)
        {
            return RHIResult<RHISurfaceRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Windows RHI surface requires a Win32Window.");
        }
        desc.platform = RHISurfacePlatform::Win32;
        desc.window_handle = static_cast<void*>(win32_window->get_native_hwnd());
        desc.application_handle = static_cast<void*>(win32_window->get_native_hinstance());
#elif WITH_MAC
        auto* mac_window = dynamic_cast<MacWindow*>(&window);
        if (mac_window == nullptr)
        {
            return RHIResult<RHISurfaceRef>::failure(
                RHIErrorCode::InvalidArgument,
                "macOS RHI surface requires a MacWindow.");
        }
        desc.platform = RHISurfacePlatform::MacOS;
        desc.window_handle = mac_window->get_metal_layer();
#elif WITH_ANDROID
        auto* android_window = dynamic_cast<AndroidWindow*>(&window);
        if (android_window == nullptr)
        {
            return RHIResult<RHISurfaceRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Android RHI surface requires an AndroidWindow.");
        }
        desc.platform = RHISurfacePlatform::Glfw;
        desc.window_handle = static_cast<void*>(android_window->get_glfw_window());
#else
        (void)window;
        return RHIResult<RHISurfaceRef>::failure(
            RHIErrorCode::Unsupported,
            "This platform does not provide an RHI surface factory.");
#endif

        const RHIStatus validation = validate_surface_desc(desc);
        if (!validation)
        {
            return RHIResult<RHISurfaceRef>::failure(
                validation.code(),
                validation.message());
        }
        return RHIResult<RHISurfaceRef>::success(
            std::make_shared<RHISurface>(std::move(desc)));
    }
}
