#pragma once

#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <bitset>

#include <vector>
#include <stdlib.h>
#include <stdint.h>
#include <iostream>

#include <vulkan/vulkan.h>

//#define VK_VERSION_1_0
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vk_mem_alloc.h>

namespace toy3d
{

    constexpr static const int VK_REQUIRED_VERSION_MAJOR = 1;
    constexpr static const int VK_REQUIRED_VERSION_MINOR = 0;

    const std::string to_string(VkResult result)
    {
        switch (result)
        {
    #define STR(r)   \
        case VK_##r: \
            return #r
            STR(NOT_READY);
            STR(TIMEOUT);
            STR(EVENT_SET);
            STR(EVENT_RESET);
            STR(INCOMPLETE);
            STR(ERROR_OUT_OF_HOST_MEMORY);
            STR(ERROR_OUT_OF_DEVICE_MEMORY);
            STR(ERROR_INITIALIZATION_FAILED);
            STR(ERROR_DEVICE_LOST);
            STR(ERROR_MEMORY_MAP_FAILED);
            STR(ERROR_LAYER_NOT_PRESENT);
            STR(ERROR_EXTENSION_NOT_PRESENT);
            STR(ERROR_FEATURE_NOT_PRESENT);
            STR(ERROR_INCOMPATIBLE_DRIVER);
            STR(ERROR_TOO_MANY_OBJECTS);
            STR(ERROR_FORMAT_NOT_SUPPORTED);
            STR(ERROR_SURFACE_LOST_KHR);
            STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
            STR(SUBOPTIMAL_KHR);
            STR(ERROR_OUT_OF_DATE_KHR);
            STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
            STR(ERROR_VALIDATION_FAILED_EXT);
            STR(ERROR_INVALID_SHADER_NV);
    #undef STR
            default:
                return "UNKNOWN_ERROR";
        }
    }

    /// @brief Helper macro to test the result of Vulkan calls which can return an error.
    #define VK_CHECK(x)                                                 \
        do                                                              \
        {                                                               \
            VkResult err = x;                                           \
            if (err)                                                    \
            {                                                           \
                std::cout << "error: " << to_string(err) << std::endl;  \
                abort();                                                \
            }                                                           \
        } while (0)
}// namespace toy3d