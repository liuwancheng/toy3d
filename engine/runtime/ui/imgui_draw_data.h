#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    class ImGuiTextureId final
    {
    public:
        constexpr ImGuiTextureId() noexcept = default;
        explicit constexpr ImGuiTextureId(std::uint64_t value) noexcept
            : value_(value)
        {}

        constexpr std::uint64_t value() const noexcept { return value_; }
        constexpr bool valid() const noexcept { return value_ != 0u; }

        friend constexpr bool operator==(
            ImGuiTextureId left,
            ImGuiTextureId right) noexcept
        {
            return left.value_ == right.value_;
        }

        friend constexpr bool operator!=(
            ImGuiTextureId left,
            ImGuiTextureId right) noexcept
        {
            return !(left == right);
        }

    private:
        std::uint64_t value_ = 0u;
    };

    constexpr ImGuiTextureId IMGUI_FONT_ATLAS_TEXTURE_ID(1u);

    struct ImGuiVertex
    {
        float position[2] = {0.0F, 0.0F};
        float uv[2] = {0.0F, 0.0F};
        std::uint32_t color = 0u;
    };

    struct ImGuiClipRect
    {
        float left = 0.0F;
        float top = 0.0F;
        float right = 0.0F;
        float bottom = 0.0F;
    };

    struct ImGuiDrawCommand
    {
        std::uint32_t element_count = 0u;
        std::uint32_t first_index = 0u;
        std::int32_t vertex_offset = 0;
        ImGuiClipRect clip_rect;
        ImGuiTextureId texture_id;
        bool reset_render_state = false;
    };

    struct ImGuiDrawData final
    {
        ImGuiDrawData() = default;
        ~ImGuiDrawData() = default;
        ImGuiDrawData(const ImGuiDrawData&) = delete;
        ImGuiDrawData& operator=(const ImGuiDrawData&) = delete;
        ImGuiDrawData(ImGuiDrawData&&) noexcept = default;
        ImGuiDrawData& operator=(ImGuiDrawData&&) noexcept = default;

        bool empty() const noexcept { return commands.empty(); }

        std::vector<ImGuiVertex> vertices;
        std::vector<std::uint8_t> indices;
        std::vector<ImGuiDrawCommand> commands;
        std::uint32_t index_stride = 0u;
        float display_position[2] = {0.0F, 0.0F};
        float display_size[2] = {0.0F, 0.0F};
        float framebuffer_scale[2] = {1.0F, 1.0F};
        std::uint32_t framebuffer_width = 0u;
        std::uint32_t framebuffer_height = 0u;
    };

    struct ImGuiFontAtlasData final
    {
        std::vector<std::uint8_t> rgba_pixels;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::uint32_t row_pitch = 0u;
        ImGuiTextureId texture_id = IMGUI_FONT_ATLAS_TEXTURE_ID;

        bool valid() const noexcept;
    };

    struct ImGuiSnapshotResult
    {
        std::unique_ptr<ImGuiDrawData> draw_data;
        std::string diagnostic;

        bool succeeded() const noexcept { return diagnostic.empty(); }
    };
}
