#include "ui/imgui_system.h"

#include "imgui.h"

#include "input/input_system.h"
#include "platform/window_interface.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        constexpr std::uint64_t MAX_IMGUI_VERTICES = 4u * 1024u * 1024u;
        constexpr std::uint64_t MAX_IMGUI_INDICES = 8u * 1024u * 1024u;
        constexpr std::uint64_t MAX_IMGUI_COMMANDS = 1024u * 1024u;
        constexpr std::uint64_t MAX_IMGUI_UPLOAD_BYTES = 128u * 1024u * 1024u;

        void set_current_context(ImGuiContext* context)
        {
            ImGui::SetCurrentContext(context);
        }

        ImTextureID encode_texture_id(ImGuiTextureId texture_id)
        {
            const std::uintptr_t encoded = static_cast<std::uintptr_t>(texture_id.value());
            return reinterpret_cast<ImTextureID>(encoded);
        }

        bool decode_texture_id(ImTextureID encoded, ImGuiTextureId& texture_id)
        {
            const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(encoded);
            texture_id = ImGuiTextureId(static_cast<std::uint64_t>(value));
            return texture_id.valid();
        }

        bool finite_pair(const ImVec2& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool finite_rect(const ImVec4& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
        }

        ImGuiKey to_imgui_key(KeyCode key)
        {
            switch (key)
            {
            case KeyCode::A:
                return ImGuiKey_A;
            case KeyCode::B:
                return ImGuiKey_B;
            case KeyCode::C:
                return ImGuiKey_C;
            case KeyCode::D:
                return ImGuiKey_D;
            case KeyCode::E:
                return ImGuiKey_E;
            case KeyCode::F:
                return ImGuiKey_F;
            case KeyCode::G:
                return ImGuiKey_G;
            case KeyCode::H:
                return ImGuiKey_H;
            case KeyCode::I:
                return ImGuiKey_I;
            case KeyCode::J:
                return ImGuiKey_J;
            case KeyCode::K:
                return ImGuiKey_K;
            case KeyCode::L:
                return ImGuiKey_L;
            case KeyCode::M:
                return ImGuiKey_M;
            case KeyCode::N:
                return ImGuiKey_N;
            case KeyCode::O:
                return ImGuiKey_O;
            case KeyCode::P:
                return ImGuiKey_P;
            case KeyCode::Q:
                return ImGuiKey_Q;
            case KeyCode::R:
                return ImGuiKey_R;
            case KeyCode::S:
                return ImGuiKey_S;
            case KeyCode::T:
                return ImGuiKey_T;
            case KeyCode::U:
                return ImGuiKey_U;
            case KeyCode::V:
                return ImGuiKey_V;
            case KeyCode::W:
                return ImGuiKey_W;
            case KeyCode::X:
                return ImGuiKey_X;
            case KeyCode::Y:
                return ImGuiKey_Y;
            case KeyCode::Z:
                return ImGuiKey_Z;
            case KeyCode::NUM_0:
                return ImGuiKey_0;
            case KeyCode::NUM_1:
                return ImGuiKey_1;
            case KeyCode::NUM_2:
                return ImGuiKey_2;
            case KeyCode::NUM_3:
                return ImGuiKey_3;
            case KeyCode::NUM_4:
                return ImGuiKey_4;
            case KeyCode::NUM_5:
                return ImGuiKey_5;
            case KeyCode::NUM_6:
                return ImGuiKey_6;
            case KeyCode::NUM_7:
                return ImGuiKey_7;
            case KeyCode::NUM_8:
                return ImGuiKey_8;
            case KeyCode::NUM_9:
                return ImGuiKey_9;
            case KeyCode::ESCAPE:
                return ImGuiKey_Escape;
            case KeyCode::ENTER:
                return ImGuiKey_Enter;
            case KeyCode::TAB:
                return ImGuiKey_Tab;
            case KeyCode::BACKSPACE:
                return ImGuiKey_Backspace;
            case KeyCode::RIGHT:
                return ImGuiKey_RightArrow;
            case KeyCode::LEFT:
                return ImGuiKey_LeftArrow;
            case KeyCode::DOWN:
                return ImGuiKey_DownArrow;
            case KeyCode::UP:
                return ImGuiKey_UpArrow;
            case KeyCode::PAGE_UP:
                return ImGuiKey_PageUp;
            case KeyCode::PAGE_DOWN:
                return ImGuiKey_PageDown;
            case KeyCode::SHIFT:
                return ImGuiKey_LeftShift;
            case KeyCode::CTRL:
                return ImGuiKey_LeftCtrl;
            case KeyCode::ALT:
                return ImGuiKey_LeftAlt;
            case KeyCode::SPACE:
                return ImGuiKey_Space;
            case KeyCode::KP_0:
                return ImGuiKey_Keypad0;
            case KeyCode::KP_1:
                return ImGuiKey_Keypad1;
            case KeyCode::KP_2:
                return ImGuiKey_Keypad2;
            case KeyCode::KP_3:
                return ImGuiKey_Keypad3;
            case KeyCode::KP_4:
                return ImGuiKey_Keypad4;
            case KeyCode::KP_5:
                return ImGuiKey_Keypad5;
            case KeyCode::KP_6:
                return ImGuiKey_Keypad6;
            case KeyCode::KP_7:
                return ImGuiKey_Keypad7;
            case KeyCode::KP_8:
                return ImGuiKey_Keypad8;
            case KeyCode::KP_9:
                return ImGuiKey_Keypad9;
            default:
                return ImGuiKey_None;
            }
        }

        int to_mouse_button(KeyCode key)
        {
            switch (key)
            {
            case KeyCode::MOUSE_LEFT:
                return 0;
            case KeyCode::MOUSE_RIGHT:
                return 1;
            case KeyCode::MOUSE_MIDDLE:
                return 2;
            case KeyCode::MOUSE_4:
                return 3;
            case KeyCode::MOUSE_5:
                return 4;
            default:
                return -1;
            }
        }

        ImGuiSnapshotResult snapshot_failure(std::size_t list_index, std::size_t command_index, const char* reason)
        {
            ImGuiSnapshotResult result;
            result.diagnostic = "ImGui snapshot draw-list " + std::to_string(list_index) + ", command " +
                                std::to_string(command_index) + ": " + reason;
            return result;
        }
    } // namespace

    bool ImGuiFontAtlasData::valid() const noexcept
    {
        if (width == 0u || height == 0u || row_pitch != width * 4u || texture_id != IMGUI_FONT_ATLAS_TEXTURE_ID)
        {
            return false;
        }
        const std::uint64_t required = static_cast<std::uint64_t>(row_pitch) * height;
        return required == rgba_pixels.size();
    }

    ImGuiSystem::~ImGuiSystem()
    {
        shutdown();
    }

    ImGuiSystemStatus ImGuiSystem::initialize(IWindow& window)
    {
        if (context_ != nullptr)
        {
            return {ImGuiSystemError::InvalidArgument, "ImGuiSystem is already initialized."};
        }
        IPlatformInput* platform_input = window.get_platform_input();
        if (platform_input == nullptr || !platform_input->capabilities().supports_interactive_imgui())
        {
            return {ImGuiSystemError::Unsupported,
                    "Platform input lacks mouse, keyboard, text, focus, or framebuffer-scale support."};
        }

        IMGUI_CHECKVERSION();
        context_ = ImGui::CreateContext();
        if (context_ == nullptr)
        {
            return {ImGuiSystemError::ThirdPartyFailure, "Dear ImGui failed to create a context."};
        }
        set_current_context(context_);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendPlatformName = "Toy3d_Input";
        io.BackendRendererName = "Toy3d_RHI";
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        int bytes_per_pixel = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);
        if (pixels == nullptr || width <= 0 || height <= 0 || bytes_per_pixel != 4 ||
            static_cast<std::uint64_t>(width) * 4u > std::numeric_limits<std::uint32_t>::max())
        {
            shutdown();
            return {ImGuiSystemError::ThirdPartyFailure, "Dear ImGui produced an invalid RGBA32 font atlas."};
        }
        const std::uint64_t byte_count = static_cast<std::uint64_t>(width) * height * 4u;
        if (byte_count == 0u || byte_count > MAX_IMGUI_UPLOAD_BYTES ||
            byte_count > std::numeric_limits<std::size_t>::max())
        {
            shutdown();
            return {ImGuiSystemError::ThirdPartyFailure, "Dear ImGui font atlas exceeds the supported size."};
        }
        font_atlas_.width = static_cast<std::uint32_t>(width);
        font_atlas_.height = static_cast<std::uint32_t>(height);
        font_atlas_.row_pitch = font_atlas_.width * 4u;
        font_atlas_.rgba_pixels.assign(pixels, pixels + static_cast<std::size_t>(byte_count));
        io.Fonts->SetTexID(encode_texture_id(IMGUI_FONT_ATLAS_TEXTURE_ID));

        InputSystem::get_instance().set_event_sink(
            [this](const InputEvent& event)
            {
                process_input_event(event);
            });
        return {};
    }

    void ImGuiSystem::shutdown() noexcept
    {
        InputSystem::get_instance().set_event_sink({});
        InputSystem::get_instance().set_capture_policy({});
        frame_active_ = false;
        font_atlas_ = {};
        if (context_ != nullptr)
        {
            set_current_context(context_);
            ImGui::DestroyContext(context_);
            context_ = nullptr;
            set_current_context(nullptr);
        }
    }

    bool ImGuiSystem::begin_frame(const IWindow& window, double delta_time)
    {
        if (context_ == nullptr || frame_active_ || !std::isfinite(delta_time))
        {
            return false;
        }
        const Extent display = window.get_display_size();
        const Extent framebuffer = window.get_framebuffer_size();
        if (display.width == 0u || display.height == 0u || framebuffer.width == 0u || framebuffer.height == 0u)
        {
            return false;
        }

        const float scale_x = static_cast<float>(framebuffer.width) / static_cast<float>(display.width);
        const float scale_y = static_cast<float>(framebuffer.height) / static_cast<float>(display.height);
        if (!std::isfinite(scale_x) || !std::isfinite(scale_y) || scale_x <= 0.0F || scale_y <= 0.0F)
        {
            return false;
        }

        set_current_context(context_);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(display.width), static_cast<float>(display.height));
        io.DisplayFramebufferScale = ImVec2(scale_x, scale_y);
        io.DeltaTime = static_cast<float>(std::max(delta_time, 1.0 / 1000.0));
        ImGui::NewFrame();
        frame_active_ = true;
        InputSystem::get_instance().set_capture_policy({io.WantCaptureMouse, io.WantCaptureKeyboard, io.WantTextInput});
        return true;
    }

    ImGuiSnapshotResult ImGuiSystem::end_frame(ImGuiTextureId viewport_texture_id,
                                               const std::vector<ImGuiTextureId>& textures)
    {
        if (context_ == nullptr || !frame_active_)
        {
            return {};
        }
        set_current_context(context_);
        ImGui::Render();
        frame_active_ = false;
        for (std::size_t i = 0; i < textures.size(); ++i)
        {
            if (textures[i].value() <= IMGUI_SCENE_VIEWPORT_TEXTURE_ID.value() ||
                std::find(textures.begin(), textures.begin() + i, textures[i]) != textures.begin() + i)
            {
                return {nullptr, "Additional UI texture IDs must be valid, unique, and outside reserved IDs."};
            }
        }
        ImGuiIO& io = ImGui::GetIO();
        InputSystem::get_instance().set_capture_policy({io.WantCaptureMouse, io.WantCaptureKeyboard, io.WantTextInput});
        const ImDrawData* source = ImGui::GetDrawData();
        return source != nullptr ? snapshot(*source, viewport_texture_id, textures) : ImGuiSnapshotResult{};
    }

    const ImGuiFontAtlasData& ImGuiSystem::font_atlas() const noexcept
    {
        return font_atlas_;
    }

    void ImGuiSystem::process_input_event(const InputEvent& event)
    {
        if (context_ == nullptr)
        {
            return;
        }
        set_current_context(context_);
        ImGuiIO& io = ImGui::GetIO();
        switch (event.type)
        {
        case InputEventType::KeyPressed:
        case InputEventType::KeyReleased:
        case InputEventType::KeyHold:
        case InputEventType::KeyDoubleClick:
        {
            const KeyEvent& key_event = static_cast<const KeyEvent&>(event);
            const ImGuiKey key = to_imgui_key(key_event.key_code);
            const bool down = event.type != InputEventType::KeyReleased;
            if (key != ImGuiKey_None)
            {
                io.AddKeyEvent(key, down);
            }
            if (key_event.key_code == KeyCode::CTRL)
            {
                io.AddKeyEvent(ImGuiMod_Ctrl, down);
            }
            else if (key_event.key_code == KeyCode::SHIFT)
            {
                io.AddKeyEvent(ImGuiMod_Shift, down);
            }
            else if (key_event.key_code == KeyCode::ALT)
            {
                io.AddKeyEvent(ImGuiMod_Alt, down);
            }
            break;
        }
        case InputEventType::MouseButtonPressed:
        case InputEventType::MouseButtonReleased:
        case InputEventType::MouseButtonHold:
        case InputEventType::MouseButtonDoubleClick:
        {
            const MouseButtonEvent& mouse_event = static_cast<const MouseButtonEvent&>(event);
            const int button = to_mouse_button(mouse_event.key_code);
            if (button >= 0)
            {
                io.AddMouseButtonEvent(button, event.type != InputEventType::MouseButtonReleased);
            }
            break;
        }
        case InputEventType::MouseMove:
        {
            const MouseMoveEvent& mouse_event = static_cast<const MouseMoveEvent&>(event);
            io.AddMousePosEvent(static_cast<float>(mouse_event.x), static_cast<float>(mouse_event.y));
            break;
        }
        case InputEventType::MouseWheel:
        {
            const MouseWheelEvent& wheel_event = static_cast<const MouseWheelEvent&>(event);
            io.AddMouseWheelEvent(wheel_event.delta_x, wheel_event.delta_y);
            break;
        }
        case InputEventType::TextInput:
        {
            const TextInputEvent& text_event = static_cast<const TextInputEvent&>(event);
            if (text_event.valid())
            {
                io.AddInputCharacter(text_event.code_point);
            }
            break;
        }
        case InputEventType::WindowFocus:
        {
            const WindowFocusEvent& focus_event = static_cast<const WindowFocusEvent&>(event);
            io.AddFocusEvent(focus_event.focused);
            break;
        }
        }
    }

    ImGuiSnapshotResult ImGuiSystem::snapshot(const ImDrawData& source, ImGuiTextureId viewport_texture_id,
                                              const std::vector<ImGuiTextureId>& textures) const
    {
        ImGuiSnapshotResult result;
        if (!source.Valid || !finite_pair(source.DisplayPos) || !finite_pair(source.DisplaySize) ||
            !finite_pair(source.FramebufferScale) || source.DisplaySize.x <= 0.0F || source.DisplaySize.y <= 0.0F ||
            source.FramebufferScale.x <= 0.0F || source.FramebufferScale.y <= 0.0F)
        {
            result.diagnostic = "ImGui snapshot contains invalid display metadata.";
            return result;
        }
        const double framebuffer_width = static_cast<double>(source.DisplaySize.x) * source.FramebufferScale.x;
        const double framebuffer_height = static_cast<double>(source.DisplaySize.y) * source.FramebufferScale.y;
        if (!std::isfinite(framebuffer_width) || !std::isfinite(framebuffer_height) || framebuffer_width <= 0.0 ||
            framebuffer_height <= 0.0 || framebuffer_width > std::numeric_limits<std::uint32_t>::max() ||
            framebuffer_height > std::numeric_limits<std::uint32_t>::max() || source.TotalVtxCount < 0 ||
            source.TotalIdxCount < 0 || static_cast<std::uint64_t>(source.TotalVtxCount) > MAX_IMGUI_VERTICES ||
            static_cast<std::uint64_t>(source.TotalIdxCount) > MAX_IMGUI_INDICES)
        {
            result.diagnostic = "ImGui snapshot extent or element count exceeds supported limits.";
            return result;
        }

        std::uint64_t command_count = 0u;
        for (int list_index = 0; list_index < source.CmdListsCount; ++list_index)
        {
            if (source.CmdLists[list_index] == nullptr)
            {
                result.diagnostic = "ImGui snapshot contains a null draw list.";
                return result;
            }
            command_count += static_cast<std::uint64_t>(source.CmdLists[list_index]->CmdBuffer.Size);
            if (command_count > MAX_IMGUI_COMMANDS)
            {
                result.diagnostic = "ImGui snapshot command count exceeds the per-frame limit.";
                return result;
            }
        }
        const std::uint64_t vertex_bytes = static_cast<std::uint64_t>(source.TotalVtxCount) * sizeof(ImGuiVertex);
        const std::uint64_t index_bytes = static_cast<std::uint64_t>(source.TotalIdxCount) * sizeof(ImDrawIdx);
        if (vertex_bytes + index_bytes > MAX_IMGUI_UPLOAD_BYTES)
        {
            result.diagnostic = "ImGui snapshot upload bytes exceed the per-frame limit.";
            return result;
        }

        auto output = std::make_unique<ImGuiDrawData>();
        output->vertices.reserve(static_cast<std::size_t>(source.TotalVtxCount));
        output->indices.reserve(static_cast<std::size_t>(index_bytes));
        output->commands.reserve(static_cast<std::size_t>(command_count));
        output->index_stride = static_cast<std::uint32_t>(sizeof(ImDrawIdx));
        output->display_position[0] = source.DisplayPos.x;
        output->display_position[1] = source.DisplayPos.y;
        output->display_size[0] = source.DisplaySize.x;
        output->display_size[1] = source.DisplaySize.y;
        output->framebuffer_scale[0] = source.FramebufferScale.x;
        output->framebuffer_scale[1] = source.FramebufferScale.y;
        output->framebuffer_width = static_cast<std::uint32_t>(framebuffer_width);
        output->framebuffer_height = static_cast<std::uint32_t>(framebuffer_height);

        std::uint64_t global_vertex_base = 0u;
        std::uint64_t global_index_base = 0u;
        for (int list_index = 0; list_index < source.CmdListsCount; ++list_index)
        {
            const ImDrawList& list = *source.CmdLists[list_index];
            for (const ImDrawVert& vertex : list.VtxBuffer)
            {
                ImGuiVertex converted;
                converted.position[0] = vertex.pos.x;
                converted.position[1] = vertex.pos.y;
                converted.uv[0] = vertex.uv.x;
                converted.uv[1] = vertex.uv.y;
                converted.color = vertex.col;
                output->vertices.push_back(converted);
            }
            const std::size_t old_index_bytes = output->indices.size();
            output->indices.resize(old_index_bytes + static_cast<std::size_t>(list.IdxBuffer.Size) * sizeof(ImDrawIdx));
            std::memcpy(output->indices.data() + old_index_bytes, list.IdxBuffer.Data,
                        static_cast<std::size_t>(list.IdxBuffer.Size) * sizeof(ImDrawIdx));

            for (int command_index = 0; command_index < list.CmdBuffer.Size; ++command_index)
            {
                const ImDrawCmd& source_command = list.CmdBuffer[command_index];
                if (source_command.UserCallback != nullptr)
                {
                    if (source_command.UserCallback != ImDrawCallback_ResetRenderState)
                    {
                        return snapshot_failure(static_cast<std::size_t>(list_index),
                                                static_cast<std::size_t>(command_index),
                                                "user render callbacks are unsupported");
                    }
                    ImGuiDrawCommand reset_command;
                    reset_command.reset_render_state = true;
                    output->commands.push_back(reset_command);
                    continue;
                }
                if (!finite_rect(source_command.ClipRect))
                {
                    return snapshot_failure(static_cast<std::size_t>(list_index),
                                            static_cast<std::size_t>(command_index),
                                            "clip rect contains a non-finite value");
                }
                ImGuiTextureId texture_id;
                if (!decode_texture_id(source_command.GetTexID(), texture_id) ||
                    (texture_id != IMGUI_FONT_ATLAS_TEXTURE_ID && texture_id != viewport_texture_id &&
                     std::find(textures.begin(), textures.end(), texture_id) == textures.end()))
                {
                    return snapshot_failure(static_cast<std::size_t>(list_index),
                                            static_cast<std::size_t>(command_index),
                                            "texture identity is not registered");
                }
                const std::uint64_t first_index = global_index_base + source_command.IdxOffset;
                const std::uint64_t vertex_offset = global_vertex_base + source_command.VtxOffset;
                const std::uint64_t end_index = first_index + source_command.ElemCount;
                if (first_index > std::numeric_limits<std::uint32_t>::max() ||
                    source_command.ElemCount > std::numeric_limits<std::uint32_t>::max() ||
                    end_index > global_index_base + static_cast<std::uint64_t>(list.IdxBuffer.Size) ||
                    vertex_offset > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
                {
                    return snapshot_failure(static_cast<std::size_t>(list_index),
                                            static_cast<std::size_t>(command_index),
                                            "draw offsets exceed the flattened payload range");
                }
                for (std::uint64_t index = first_index; index < end_index; ++index)
                {
                    const std::uint64_t local_index = index - global_index_base;
                    if (vertex_offset + list.IdxBuffer[static_cast<int>(local_index)] >=
                        global_vertex_base + static_cast<std::uint64_t>(list.VtxBuffer.Size))
                    {
                        return snapshot_failure(static_cast<std::size_t>(list_index),
                                                static_cast<std::size_t>(command_index),
                                                "indexed vertex reference is out of range");
                    }
                }

                ImGuiDrawCommand command;
                command.element_count = source_command.ElemCount;
                command.first_index = static_cast<std::uint32_t>(first_index);
                command.vertex_offset = static_cast<std::int32_t>(vertex_offset);
                command.clip_rect = {source_command.ClipRect.x, source_command.ClipRect.y, source_command.ClipRect.z,
                                     source_command.ClipRect.w};
                command.texture_id = texture_id;
                output->commands.push_back(command);
            }
            global_vertex_base += static_cast<std::uint64_t>(list.VtxBuffer.Size);
            global_index_base += static_cast<std::uint64_t>(list.IdxBuffer.Size);
        }

        if (!output->empty())
        {
            result.draw_data = std::move(output);
        }
        return result;
    }
} // namespace toy3d
