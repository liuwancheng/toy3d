#pragma once

#include "drivers/rhi/rhi_public_definitions.h"
#include "drivers/rhi/rhi_result.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace toy3d
{
    // Render-thread-owned retirement queue. Callbacks may destroy native
    // objects only after the associated submit serial has completed.
    class VulkanDeferredDeletionQueue final
    {
    public:
        using DeletionCallback = std::function<void(VkDevice)>;

        RHIStatus enqueue(RHISubmitSerial serial, DeletionCallback callback);
        void release_completed(VkDevice device, RHISubmitSerial completed_serial);
        void release_all(VkDevice device);
        std::size_t pending_count() const;

    private:
        struct Entry
        {
            RHISubmitSerial serial = 0;
            DeletionCallback callback;
        };

        std::vector<Entry> entries;
    };
}
