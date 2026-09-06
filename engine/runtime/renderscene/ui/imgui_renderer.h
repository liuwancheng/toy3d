#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/global_shader_type.h"
#include "ui/imgui_draw_data.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class ShaderMapProgram;

    const GlobalShaderType& imgui_global_shader_type();

    struct ImGuiPassTarget
    {
        RHITextureViewRef color_view;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        PixelFormat format = PixelFormat::B8G8R8A8UNorm;
        std::uint32_t sample_count = 1u;
        RHILoadOperation load = RHILoadOperation::Load;
        RHIClearValue clear_value;
    };

    class ImGuiRenderer final
    {
    public:
        ImGuiRenderer() = default;
        ~ImGuiRenderer() = default;

        ImGuiRenderer(const ImGuiRenderer&) = delete;
        ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

        RHIStatus initialize(
            RHIDevice& device,
            RHIShaderProgramCache& shader_program_cache,
            const GlobalShaderMap& global_shader_map,
            const ImGuiFontAtlasData& font_atlas);
        RHIStatus record_font_upload(
            RHIGraphicsCommandContext& context,
            const ImGuiFontAtlasData& font_atlas);
        void publish_bootstrap_complete() noexcept;
        void release() noexcept;

        RHIStatus render(
            RHIDevice& device,
            RHIGraphicsCommandContext& context,
            const ImGuiDrawData& draw_data,
            const ImGuiPassTarget& target);
        RHIStatus publish_frame_submission(
            RHIQueueCompletionValue completion_value) noexcept;
        void discard_frame_recording() noexcept;

        bool initialized() const noexcept;
        bool ready() const noexcept;

    private:
        struct BufferPage
        {
            RHIBufferRef vertex_buffer;
            RHIBufferRef index_buffer;
            std::size_t vertex_capacity = 0u;
            std::size_t index_capacity = 0u;
            RHIAccess vertex_access = RHIAccess::Common;
            RHIAccess index_access = RHIAccess::Common;
            RHIQueueCompletionValue completion_value = 0u;
            bool recording = false;
        };

        RHIResult<std::size_t> acquire_buffer_page(
            RHIDevice& device,
            std::size_t vertex_bytes,
            std::size_t index_bytes);

        static constexpr std::size_t INVALID_PAGE_INDEX =
            std::numeric_limits<std::size_t>::max();

        const ShaderMapBinding* constant_buffer_binding_ = nullptr;
        const ShaderMapBinding::ConstantMember* projection_binding_ = nullptr;
        const ShaderMapBinding* font_texture_binding_ = nullptr;
        const ShaderMapBinding* font_sampler_binding_ = nullptr;
        const ShaderMapProgram* shader_program_ = nullptr;
        RHIShaderProgramRef rhi_program_;
        RHITextureRef font_texture_;
        RHITextureViewRef font_texture_view_;
        RHISamplerRef font_sampler_;
        RHIGraphicsPipelineRef pipeline_;
        std::vector<BufferPage> buffer_pages_;
        std::size_t recording_page_index_ = INVALID_PAGE_INDEX;
        bool bootstrap_complete_ = false;
    };
}
