#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "math/integer_vector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    using RHIRect = IntRect;

    struct RHIOffset3D
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t z = 0;
    };

    struct RHIExtent3D
    {
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        std::uint32_t depth = 1;
    };

    struct RHIViewport
    {
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        float min_depth = 0.0F;
        float max_depth = 1.0F;
    };

    struct RHIResourceTransition
    {
        RHIResourceRef resource;
        RHISubresourceRange subresources;
        RHIAccess before = RHIAccess::Unknown;
        RHIAccess after = RHIAccess::Unknown;
    };

    struct RHIBufferCopyDesc
    {
        RHIBufferRef source;
        RHIBufferRef destination;
        std::uint64_t source_offset = 0;
        std::uint64_t destination_offset = 0;
        std::uint64_t size = 0;
    };

    // Upload data is copied into backend-owned staging storage before this
    // command returns. The staging storage remains alive until the command
    // list's frame slot has completed on the GPU.
    struct RHIBufferUploadDesc
    {
        RHIBufferRef destination;
        std::uint64_t destination_offset = 0;
        RHIInitialData source;
    };

    struct RHITransientUniformDataDesc
    {
        RHIInitialData source;
        std::string debug_name;
    };

    struct RHIUniformBufferSlice
    {
        RHIBufferRef buffer;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    struct RHITextureCopyLocation
    {
        RHITextureRef texture;
        std::uint32_t mip = 0;
        std::uint32_t layer = 0;
        RHIOffset3D offset;
    };

    struct RHITextureCopyDesc
    {
        RHITextureCopyLocation source;
        RHITextureCopyLocation destination;
        RHIExtent3D extent;
    };

    struct RHITexturePixelReadbackDesc
    {
        RHITextureCopyLocation source;
        RHIReadbackRef destination;
    };

    struct RHITextureReadbackDesc
    {
        RHITextureCopyLocation source;
        Extent extent;
        RHIReadbackRef destination;
    };

    struct RHITextureUploadDesc
    {
        RHITextureCopyLocation destination;
        RHIExtent3D extent;
        RHIInitialData source;
    };

    struct RHIColorAttachmentDesc
    {
        RHITextureViewRef view;
        RHITextureViewRef resolve_view;
        RHILoadOperation load = RHILoadOperation::Load;
        RHIStoreOperation store = RHIStoreOperation::Store;
        RHIClearValue clear_value;
    };

    struct RHIDepthStencilAttachmentDesc
    {
        RHITextureViewRef view;
        RHILoadOperation depth_load = RHILoadOperation::Load;
        RHIStoreOperation depth_store = RHIStoreOperation::Store;
        RHILoadOperation stencil_load = RHILoadOperation::Load;
        RHIStoreOperation stencil_store = RHIStoreOperation::Store;
        RHIClearValue clear_value;
    };

    struct RHIRenderPassDesc
    {
        std::vector<RHIColorAttachmentDesc> color_attachments;
        RHIDepthStencilAttachmentDesc depth_stencil_attachment;
        bool has_depth_stencil_attachment = false;
        std::string debug_name;
    };

    struct RHIVertexBufferBinding
    {
        RHIBufferRef buffer;
        std::uint64_t offset = 0;
        std::uint32_t stride = 0;
    };

    struct RHIIndexBufferBinding
    {
        RHIBufferRef buffer;
        std::uint64_t offset = 0;
        RHIIndexFormat format = RHIIndexFormat::UInt16;
    };

    struct RHIGraphicsBindings
    {
        RHIBindingSetRef global;
        RHIBindingSetRef view;
        RHIBindingSetRef pass;
        RHIBindingSetRef material;
        RHIBindingSetRef object;
    };

    namespace rhi_detail
    {
        // Resolved bindings are an RHI/backend hand-off; RenderScene only owns
        // the five logical snapshots and never observes target mappings here.
        struct ResolvedBinding
        {
            RHIBindingLayoutEntry layout;
            RHIBindingValue value;
            RHIBindingSetRef source_set;
        };

        RHIResult<std::vector<ResolvedBinding>> resolve_graphics_bindings(const RHIGraphicsPipelineRef& pipeline,
                                                                          const RHIGraphicsBindings& bindings);
    } // namespace rhi_detail

    struct RHIDrawArgs
    {
        std::uint32_t vertex_count = 0;
        std::uint32_t instance_count = 1;
        std::uint32_t first_vertex = 0;
        std::uint32_t first_instance = 0;
    };

    struct RHIDrawIndexedArgs
    {
        std::uint32_t index_count = 0;
        std::uint32_t instance_count = 1;
        std::uint32_t first_index = 0;
        std::int32_t vertex_offset = 0;
        std::uint32_t first_instance = 0;
    };

    RHIStatus validate_resource_transition(const RHIResourceTransition& transition);
    RHIStatus validate_buffer_copy_desc(const RHIBufferCopyDesc& desc);
    RHIStatus validate_buffer_upload_desc(const RHIBufferUploadDesc& desc);
    RHIStatus validate_transient_uniform_data_desc(const RHITransientUniformDataDesc& desc);
    RHIStatus validate_texture_copy_desc(const RHITextureCopyDesc& desc);
    RHIStatus validate_texture_pixel_readback_desc(const RHITexturePixelReadbackDesc& desc);
    RHIStatus validate_texture_readback_desc(const RHITextureReadbackDesc& desc);
    RHIStatus validate_texture_upload_desc(const RHITextureUploadDesc& desc);
    RHIStatus validate_render_pass_desc(const RHIRenderPassDesc& desc);
    RHIStatus validate_graphics_bindings(const RHIGraphicsBindings& bindings);
    RHIStatus validate_draw_args(const RHIDrawArgs& args);
    RHIStatus validate_draw_indexed_args(const RHIDrawIndexedArgs& args);
} // namespace toy3d
