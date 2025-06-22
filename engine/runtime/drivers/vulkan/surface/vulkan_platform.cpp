#include "vulkan_platform.h"
#include "vulkan/vk_com.h"
#include "engine.h"
#if WITH_WIN64
#include "platform/win/win32_window.h"
#include "vulkan/vulkan_win32.h"
#elif WITH_MAC
#include "platform/mac/mac_window.h"
#elif WITH_ANDROID
#include "platform/android/android_window.h"
#endif

namespace toy3d
{
    extern Engine g_engine;

    const char** VulkanGenericPlatform::get_required_extensions(uint32_t* count)
    {
#if WITH_WIN64
        const char* required_extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME
        };

        *count = 2;
        return required_extensions;
#elif WITH_MAC
        return glfwGetRequiredInstanceExtensions(count);
#elif WITH_ANDROID
        return glfwGetRequiredInstanceExtensions(count);
#endif
    }

    void VulkanGenericPlatform::create_window_surface(VkInstance instance, VkSurfaceKHR & surface)
    {
        IWindow* win = g_engine.get_window();
#if WITH_WIN64
        Win32Window* win32 = static_cast<Win32Window*>(win);
        VkWin32SurfaceCreateInfoKHR surfaceInfo;
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hinstance = win32->get_native_hinstance();
        surfaceInfo.hwnd = win32->get_native_hwnd();
        vkCreateWin32SurfaceKHR(instance, &surfaceInfo, NULL, &surface);
#elif WITH_ANDROID
        VkSurfaceKHR surface;
        AndroidWindow* ad_win = static_cast<AndroidWindow*>(win);
        glfwCreateWindowSurface(instance, ad_win->get_glfw_window(), nullptr, &surface);
#elif WITH_MAC
        VkSurfaceKHR surface;
        MacWindow* mac_win = static_cast<MacWindow*>(win);
        glfwCreateWindowSurface(instance, mac_win->get_glfw_window(), nullptr, &surface);
#endif
    }
}