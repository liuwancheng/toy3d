#pragma once
#include <vulkan/vulkan.h>

//#define VK_VERSION_1_0
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#include "core/misc/pch.h"
#include "core/misc/logger.h"
#include "rhi/rhi_definitions.h"
#include "rhi/rhi_inilitializer.h"
#include "rhi/rhi_resource.h"
#include "vk_cast.h"

namespace toy3d
{
    constexpr const int VK_REQUIRED_VERSION_MAJOR = 1;
    constexpr const int VK_REQUIRED_VERSION_MINOR = 0;

 
    /// @brief Helper macro to test the result of Vulkan calls which can return an error.
    #define VK_CHECK(x)                                                 \
        do                                                              \
        {                                                               \
            VkResult err = x;                                           \
            if (err)                                                    \
            {                                                           \
                TOY_LOG_ERROR("error: {}", cast_vk_error(err));               \
                abort();                                                \
            }                                                           \
        } while (0)
}// namespace toy3d