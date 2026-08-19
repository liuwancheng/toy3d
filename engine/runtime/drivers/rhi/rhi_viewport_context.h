#pragma once

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    inline bool rhi_is_recoverable_viewport_status(const RHIStatus& status)
    {
        return status.code() == RHIErrorCode::NotReady ||
            status.code() == RHIErrorCode::OutOfDate ||
            status.code() == RHIErrorCode::Suboptimal;
    }

    struct RHIViewportContextDesc
    {
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        std::uint32_t image_count = 2;
        RHIFormat format = RHIFormat::B8G8R8A8UNorm;
        RHIPresentMode present_mode = RHIPresentMode::Fifo;
        std::string debug_name;
    };

    // A frame context is valid only between RHIViewportContext::begin_frame()
    // and either end_frame() or abort_frame(). It exposes the current presentation image as a normal
    // render-graph external resource while keeping acquire synchronization
    // private to the viewport implementation.
    class RHIFrameContext
    {
    public:
        RHIFrameContext() = default;
        virtual ~RHIFrameContext() = default;

        RHIFrameContext(const RHIFrameContext&) = delete;
        RHIFrameContext& operator=(const RHIFrameContext&) = delete;

        virtual const RHITextureRef& present_texture() const = 0;
        virtual const RHITextureViewRef& present_view() const = 0;

        virtual std::uint32_t width() const = 0;
        virtual std::uint32_t height() const = 0;

        // Recording contexts are frame-local so their allocators can be
        // recycled only after this frame's queue completion value has completed.
        virtual RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>
            create_graphics_command_context() = 0;
    };

    // Owns the presentation lifecycle for one native surface. RenderScene
    // records ordered pass command lists, but never acquires or presents a
    // swapchain image directly.
    class RHIViewportContext
    {
    public:
        RHIViewportContext() = default;
        virtual ~RHIViewportContext() = default;

        RHIViewportContext(const RHIViewportContext&) = delete;
        RHIViewportContext& operator=(const RHIViewportContext&) = delete;

        virtual RHIResult<std::unique_ptr<RHIFrameContext>> begin_frame() = 0;

        virtual RHIStatus end_frame(
            std::unique_ptr<RHIFrameContext> frame,
            const std::vector<RHICommandListRef>& command_lists) = 0;

        // Consumes an acquired frame after recording cannot continue. The
        // backend must discharge acquire synchronization without submitting
        // discarded command lists. A successful abort leaves the slot reusable;
        // an unrecoverable backend failure must reject subsequent frames.
        virtual RHIStatus abort_frame(std::unique_ptr<RHIFrameContext> frame) = 0;

        // Resize is deferred until a later begin_frame() can safely replace
        // all in-flight presentation images.
        virtual RHIStatus request_resize(
            std::uint32_t width,
            std::uint32_t height) = 0;
    };
}
