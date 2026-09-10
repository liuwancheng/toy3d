#pragma once

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "math/integer_vector.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace toy3d
{
    inline bool rhi_is_recoverable_viewport_status(const RHIStatus& status)
    {
        return status.code() == RHIErrorCode::NotReady || status.code() == RHIErrorCode::OutOfDate ||
               status.code() == RHIErrorCode::Suboptimal;
    }

    inline RHIStatus rhi_normalize_incomplete_acquired_frame_status(const RHIStatus& status, const char* operation)
    {
        // Once a backend has acquired presentation ownership, a failure to
        // discharge that ownership cannot be retried as an ordinary viewport
        // resize/minimize outcome. Preserve an existing terminal diagnostic,
        // but promote an otherwise recoverable code to BackendFailure.
        if (status || !rhi_is_recoverable_viewport_status(status))
        {
            return status;
        }
        return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                  std::string(operation) +
                                      " could not complete an acquired viewport frame: " + status.message());
    }

    inline RHIStatus rhi_normalize_submitted_presentation_status(const RHIStatus& status, const char* operation)
    {
        // A successful business submit has already consumed the acquired
        // synchronization. NotReady cannot describe that presentation
        // boundary and must become terminal instead of inviting a retry that
        // would roll back submitted work.
        if (status.code() != RHIErrorCode::NotReady)
        {
            return status;
        }
        return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                  std::string(operation) +
                                      " returned NotReady after business work was submitted: " + status.message());
    }

    struct RHIFrameEndResult
    {
        RHIQueueCompletionValue completion_value = 0;
        RHIStatus presentation_status;
    };

    struct RHIViewportContextDesc
    {
        Extent extent{1, 1};
        std::uint32_t image_count = 2;
        PixelFormat format = PixelFormat::B8G8R8A8UNorm;
        RHIPresentMode present_mode = RHIPresentMode::Fifo;
        std::string debug_name;
    };

    // A frame context is valid only between RHIViewportContext::begin_frame()
    // and either end_frame() or abort_frame(). It exposes the current presentation image as a normal
    // render-graph external resource while keeping acquire synchronization
    // private to the viewport implementation.
    class RHIFrameContext : public RHIObject
    {
      public:
        explicit RHIFrameContext(const RHIDevice& owner) : RHIObject(owner) {}
        ~RHIFrameContext() override = default;

        RHIFrameContext(const RHIFrameContext&) = delete;
        RHIFrameContext& operator=(const RHIFrameContext&) = delete;

        virtual const RHITextureRef& present_texture() const = 0;
        virtual const RHITextureViewRef& present_view() const = 0;

        virtual Extent extent() const = 0;

        // Recording contexts are frame-local so their allocators can be
        // recycled only after this frame's queue completion value has completed.
        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> create_graphics_command_context()
        {
            RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> result = create_graphics_command_context_impl();
            if (!result)
            {
                return result;
            }
            if (!result.value() || !result.value()->is_owned_by(*owner_device()))
            {
                return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::failure(
                    RHIErrorCode::BackendFailure,
                    "Frame context returned no graphics context or one created by another device.");
            }
            return result;
        }

      protected:
        virtual RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> create_graphics_command_context_impl() = 0;
    };

    // Owns the presentation lifecycle for one native surface. RenderScene
    // records ordered pass command lists, but never acquires or presents a
    // swapchain image directly.
    class RHIViewportContext : public RHIObject
    {
      public:
        explicit RHIViewportContext(const RHIDevice& owner, std::string debug_name = {})
            : RHIObject(owner, std::move(debug_name))
        {
        }
        virtual ~RHIViewportContext() = default;

        RHIViewportContext(const RHIViewportContext&) = delete;
        RHIViewportContext& operator=(const RHIViewportContext&) = delete;

        RHIResult<std::unique_ptr<RHIFrameContext>> begin_frame()
        {
            RHIResult<std::unique_ptr<RHIFrameContext>> result = begin_frame_impl();
            if (!result)
            {
                return result;
            }
            RHIFrameContext* frame = result.value().get();
            const bool valid_frame = frame != nullptr && frame->is_owned_by(*owner_device()) &&
                                     frame->present_texture() && frame->present_view() &&
                                     frame->present_texture()->is_owned_by(*owner_device()) &&
                                     frame->present_view()->is_owned_by(*owner_device()) &&
                                     frame->present_view()->texture() == frame->present_texture();
            if (valid_frame)
            {
                return result;
            }

            const RHIStatus recovery = abort_frame(std::move(result).value());
            if (!recovery && !rhi_is_recoverable_viewport_status(recovery))
            {
                return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(recovery.code(), recovery.message());
            }
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(
                RHIErrorCode::BackendFailure,
                "Viewport backend returned an incomplete frame or presentation resources from another device.");
        }

        // Outer success is the business-submit truth. Presentation status is
        // reported separately because present cannot roll submitted work back.
        virtual RHIResult<RHIFrameEndResult> end_frame(std::unique_ptr<RHIFrameContext> frame,
                                                       const std::vector<RHICommandListRef>& command_lists) = 0;

        // Consumes an acquired frame after recording cannot continue. The
        // backend must discharge acquire synchronization without submitting
        // discarded command lists. A successful abort leaves the slot reusable;
        // an unrecoverable backend failure must reject subsequent frames.
        virtual RHIStatus abort_frame(std::unique_ptr<RHIFrameContext> frame) = 0;

        // Resize is deferred until a later begin_frame() can safely replace
        // all in-flight presentation images.
        virtual RHIStatus request_resize(const Extent& extent) = 0;

      protected:
        virtual RHIResult<std::unique_ptr<RHIFrameContext>> begin_frame_impl() = 0;
    };
} // namespace toy3d
