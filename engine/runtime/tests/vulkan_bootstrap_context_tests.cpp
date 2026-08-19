#include "drivers/rhi/rhi_factory.h"
#include "drivers/rhi/rhi_queue.h"
#include "platform/rhi_surface_factory.h"
#include "platform/win/win32_window.h"
#include "renderscene/resources/render_resource_cache.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }
}

int main()
{
    using namespace toy3d;

    Win32Window window(GetModuleHandle(nullptr));
    check(window.get_native_hwnd() != nullptr,
        "The Vulkan bootstrap test requires a native Win32 window");
    if (window.get_native_hwnd() != nullptr)
    {
        ShowWindow(window.get_native_hwnd(), SW_HIDE);
    }

    auto surface_result = create_rhi_surface(window);
    check(static_cast<bool>(surface_result),
        "The Vulkan bootstrap test must create a public RHI surface");
    auto device_result = create_default_rhi_device();
    check(static_cast<bool>(device_result),
        "The configured Vulkan backend must create an RHI device");

    std::unique_ptr<RHIDevice> device;
    bool initialized = false;
    if (surface_result && device_result)
    {
        device = std::move(device_result).value();
        RHIDeviceDesc device_desc;
        device_desc.primary_surface = std::move(surface_result).value();
        device_desc.enable_validation = true;
        device_desc.debug_name = "VulkanBootstrapContextTestDevice";
        const RHIStatus initialize_status = device->initialize(device_desc);
        check(static_cast<bool>(initialize_status),
            "The Vulkan bootstrap test device must initialize");
        initialized = static_cast<bool>(initialize_status);
    }

    std::unique_ptr<RHIGraphicsCommandContext> context;
    RHIBufferRef buffer;
    RHICommandListRef command_list;
    RHISubmitInfo submit_info;
    std::unique_ptr<RenderResourceCache> resource_cache;
    if (initialized)
    {
        auto context_result = device->create_graphics_command_context();
        check(static_cast<bool>(context_result),
            "Vulkan must create a device-level graphics command context");
        if (context_result)
        {
            context = std::move(context_result).value();
        }
    }

    if (context)
    {
        check(static_cast<bool>(context->begin_recording(
                "Vulkan bootstrap context test")),
            "The device-level context must begin one command list");

        RHIBufferDesc buffer_desc;
        buffer_desc.size = sizeof(std::uint32_t) * 4;
        buffer_desc.usage = rhi_enum_or(
            RHIResourceUsage::VertexBuffer,
            RHIResourceUsage::CopyDestination);
        buffer_desc.initial_access = RHIAccess::Common;
        buffer_desc.debug_name = "VulkanBootstrapContextTestBuffer";
        auto buffer_result = device->create_buffer(buffer_desc);
        check(static_cast<bool>(buffer_result),
            "The bootstrap test must create a GPU-only destination buffer");
        if (buffer_result)
        {
            buffer = std::move(buffer_result).value();
        }
    }

    if (context && buffer)
    {
        RHIResourceTransition to_copy;
        to_copy.resource = buffer;
        to_copy.before = RHIAccess::Common;
        to_copy.after = RHIAccess::CopyDestination;
        check(static_cast<bool>(context->transition_resources({to_copy})),
            "The bootstrap buffer must transition to CopyDestination");

        const std::array<std::uint32_t, 4> source = {1, 2, 3, 4};
        RHIBufferUploadDesc upload;
        upload.destination = buffer;
        upload.source.data = source.data();
        upload.source.size = sizeof(source);
        check(static_cast<bool>(context->upload_buffer(upload)),
            "The device-level context must copy bootstrap data into backend-owned staging");

        RHIResourceTransition to_vertex;
        to_vertex.resource = buffer;
        to_vertex.before = RHIAccess::CopyDestination;
        to_vertex.after = RHIAccess::VertexBuffer;
        check(static_cast<bool>(context->transition_resources({to_vertex})),
            "The bootstrap buffer must transition to its first consumer access");

        auto command_list_result = context->finish_recording();
        check(static_cast<bool>(command_list_result),
            "The device-level context must finish an immutable command list");
        if (command_list_result)
        {
            command_list = std::move(command_list_result).value();
        }
    }

    if (command_list)
    {
        submit_info.command_lists.push_back(command_list);
        submit_info.debug_name = "Vulkan bootstrap context test submit";
        auto submit_result = device->graphics_queue().submit(submit_info);
        check(static_cast<bool>(submit_result),
            "The generic graphics queue must submit a device-level command list");
        if (submit_result)
        {
            const RHIQueueCompletionValue completion_value =
                submit_result.value().completion_value;
            check(command_list->state() == RHICommandListState::Submitted,
                "A successful generic submit must advance command-list state");
            check(static_cast<bool>(
                    device->graphics_queue().wait_for_value(completion_value)),
                "Bootstrap initialization must be able to wait for its explicit completion value");
            check(device->graphics_queue().completed_value() >= completion_value,
                "The waited bootstrap completion value must become observable");
        }
    }

    if (initialized)
    {
        resource_cache = std::make_unique<RenderResourceCache>(
            create_builtin_render_resource_placeholders());
        check(resource_cache->is_valid() &&
                !resource_cache->rhi_placeholders_initialized(),
            "Built-in CPU placeholders must be valid but unpublished before bootstrap");
        const RHIStatus bootstrap_status =
            resource_cache->initialize_rhi_placeholders(*device);
        check(static_cast<bool>(bootstrap_status),
            "Renderer placeholder bootstrap must complete on the real Vulkan queue");
        check(resource_cache->rhi_placeholders_initialized(),
            "Renderer placeholder RHI resources must publish after completion");

        const auto color = resource_cache->resolve_texture_rhi(
            TextureRenderResourceId{}, TextureColorSemantic::Color);
        const auto linear = resource_cache->resolve_texture_rhi(
            TextureRenderResourceId{}, TextureColorSemantic::Linear);
        const auto normal = resource_cache->resolve_texture_rhi(
            TextureRenderResourceId{}, TextureColorSemantic::Normal);
        check(color.state == RenderResourceResolveState::Placeholder && color &&
                color.version->texture->desc().format ==
                    RHIFormat::R8G8B8A8UNormSRGB,
            "Color misses must resolve the completed sRGB checkerboard placeholder");
        check(linear.state == RenderResourceResolveState::Placeholder && linear &&
                linear.version->texture->desc().format ==
                    RHIFormat::R8G8B8A8UNorm,
            "Linear misses must resolve the completed linear white placeholder");
        check(normal.state == RenderResourceResolveState::Placeholder && normal &&
                normal.version->texture->desc().format ==
                    RHIFormat::R8G8B8A8UNorm,
            "Normal misses must resolve the completed linear normal placeholder");
    }

    submit_info.command_lists.clear();
    command_list.reset();
    buffer.reset();
    context.reset();
    resource_cache.reset();
    if (initialized)
    {
        check(static_cast<bool>(device->shutdown()),
            "The Vulkan device must shut down after bootstrap payload retirement");
    }
    device.reset();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " Vulkan bootstrap context check(s) failed.\n";
        return 1;
    }
    std::cout << "Vulkan bootstrap context checks passed.\n";
    return 0;
}
