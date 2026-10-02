#pragma once

#include "ui/imgui_draw_data.h"

#include <string>

struct ImGuiContext;
struct ImDrawData;

namespace toy3d
{
    class IWindow;
    struct InputEvent;

    enum class ImGuiSystemError
    {
        None,
        InvalidArgument,
        Unsupported,
        ThirdPartyFailure
    };

    struct ImGuiSystemStatus
    {
        ImGuiSystemError code = ImGuiSystemError::None;
        std::string message;

        bool succeeded() const noexcept
        {
            return code == ImGuiSystemError::None;
        }
    };

    class ImGuiSystem final
    {
      public:
        ImGuiSystem() = default;
        ~ImGuiSystem();

        ImGuiSystem(const ImGuiSystem&) = delete;
        ImGuiSystem& operator=(const ImGuiSystem&) = delete;

        ImGuiSystemStatus initialize(IWindow& window);
        void shutdown() noexcept;
        bool begin_frame(const IWindow& window, double delta_time);
        ImGuiSnapshotResult end_frame(ImGuiTextureId viewport_texture_id = {},
                                      const std::vector<ImGuiTextureId>& textures = {});
        const ImGuiFontAtlasData& font_atlas() const noexcept;
        bool initialized() const noexcept
        {
            return context_ != nullptr;
        }

      private:
        void process_input_event(const InputEvent& event);
        ImGuiSnapshotResult snapshot(const ImDrawData& source, ImGuiTextureId viewport_texture_id,
                                     const std::vector<ImGuiTextureId>& textures) const;

        ImGuiContext* context_ = nullptr;
        ImGuiFontAtlasData font_atlas_;
        bool frame_active_ = false;
    };
} // namespace toy3d
