#include "panels/property_widgets.h"

#include <algorithm>
#include <cmath>

#include "imgui_internal.h"

namespace toy3d
{
    bool begin_property_row(const char* label, float trailing_width)
    {
        ImGui::PushID(label);
        if (!ImGui::BeginTable("##Property", 2,
                               ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH |
                                   ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::PopID();
            return false;
        }
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight() + 4.0f);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(37, 37, 38, 255));
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        const auto position = ImGui::GetCursorScreenPos();
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        ImGui::GetWindowDrawList()->PushClipRect(
            position, ImVec2(position.x + width, position.y + ImGui::GetFrameHeight()), true);
        ImGui::TextUnformatted(label);
        ImGui::GetWindowDrawList()->PopClipRect();
        if (ImGui::IsItemHovered() && ImGui::CalcTextSize(label).x > width)
        {
            ImGui::SetTooltip("%s", label);
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(18, 18, 19, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(47, 47, 49, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(55, 55, 58, 255));
        ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - trailing_width));
        return true;
    }
    void end_property_row()
    {
        // Closing a table can submit another item. History must still observe the value widget.
        const auto item = ImGui::GetCurrentContext()->LastItemData;
        ImGui::PopStyleColor(3);
        ImGui::EndTable();
        ImGui::PopID();
        ImGui::GetCurrentContext()->LastItemData = item;
    }
    bool property_bool(const char* label, bool* value)
    {
        if (!begin_property_row(label))
        {
            return false;
        }
        const bool changed = ImGui::Checkbox("##Value", value);
        end_property_row();
        return changed;
    }
    bool property_float(const char* label, float* value, float speed, float minimum, float maximum, const char* format)
    {
        if (!begin_property_row(label))
        {
            return false;
        }
        const bool changed = ImGui::DragFloat("##Value", value, speed, minimum, maximum, format);
        end_property_row();
        return changed;
    }
    bool property_float_n(const char* label, float* value, int count, float speed)
    {
        if (!begin_property_row(label))
        {
            return false;
        }
        const bool changed = count == 2   ? ImGui::DragFloat2("##Value", value, speed)
                             : count == 3 ? ImGui::DragFloat3("##Value", value, speed)
                                          : ImGui::DragFloat4("##Value", value, speed);
        end_property_row();
        return changed;
    }
    bool property_int(const char* label, int* value, float speed)
    {
        if (!begin_property_row(label))
        {
            return false;
        }
        const bool changed = ImGui::DragInt("##Value", value, speed);
        end_property_row();
        return changed;
    }
    bool property_double(const char* label, double* value, double step, double fast_step, const char* format)
    {
        if (!begin_property_row(label))
        {
            return false;
        }
        const bool changed = ImGui::InputDouble("##Value", value, step, fast_step, format);
        end_property_row();
        return changed;
    }
    bool property_slider_float(const char* label, float* value, float minimum, float maximum, const char* format)
    {
        if (!begin_property_row(label))
        {
            return false;
        }
        const bool changed = ImGui::SliderFloat("##Value", value, minimum, maximum, format);
        end_property_row();
        return changed;
    }
    bool property_slider_int(const char* label, int* value, int minimum, int maximum)
    {
        if (!begin_property_row(label))
        {
            return false;
        }
        const bool changed = ImGui::SliderInt("##Value", value, minimum, maximum);
        end_property_row();
        return changed;
    }
    bool property_combo(const char* label, int* value, const char* const items[], int count)
    {
        if (!begin_property_row(label))
        {
            return false;
        }
        const bool changed = ImGui::Combo("##Value", value, items, count);
        end_property_row();
        return changed;
    }
    bool property_color_value(const char* id, float* value, bool alpha, float width)
    {
        ImGui::PushID(id);
        ImGui::BeginGroup();
        const auto display_channel = [](float linear)
        {
            const float bounded = std::max(0.0f, std::min(1.0f, linear));
            return bounded <= 0.0031308f ? bounded * 12.92f : 1.055f * std::pow(bounded, 1.0f / 2.4f) - 0.055f;
        };
        const ImVec4 display(display_channel(value[0]), display_channel(value[1]), display_channel(value[2]),
                             alpha ? value[3] : 1.0f);
        const auto flags = ImGuiColorEditFlags_NoDragDrop |
                           (alpha ? ImGuiColorEditFlags_AlphaPreviewHalf : ImGuiColorEditFlags_NoAlpha);
        if (ImGui::ColorButton("##Bar", display, flags,
                               ImVec2(width > 0.0f ? width : ImGui::CalcItemWidth(), ImGui::GetFrameHeight())))
        {
            ImGui::OpenPopup("Color");
        }
        bool changed = false;
        ImGuiWindow* picker_window = nullptr;
        if (ImGui::BeginPopup("Color"))
        {
            picker_window = ImGui::GetCurrentWindow();
            ImGui::TextUnformatted("Linear RGB");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
            const auto picker_flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR |
                                      ImGuiColorEditFlags_NoSidePreview |
                                      (alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_NoAlpha);
            changed = alpha ? ImGui::ColorPicker4("##Picker", value, picker_flags)
                            : ImGui::ColorPicker3("##Picker", value, picker_flags);
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
        auto& context = *ImGui::GetCurrentContext();
        // Like ColorEdit, expose the active popup child so existing history sees its drag lifetime.
        if (picker_window && context.ActiveId != 0 && context.ActiveIdWindow == picker_window)
        {
            context.LastItemData.ID = context.ActiveId;
        }
        if (changed && context.LastItemData.ID != 0)
        {
            ImGui::MarkItemEdited(context.LastItemData.ID);
        }
        ImGui::PopID();
        return changed;
    }
    bool property_color(const char* label, float* value, bool alpha)
    {
        if (!begin_property_row(label))
        {
            return false;
        }
        const bool changed = property_color_value("##Value", value, alpha);
        end_property_row();
        return changed;
    }
    bool property_action_button(const char* id, PropertyAction action, const char* tooltip)
    {
        const float size = ImGui::GetFrameHeight();
        const auto position = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
        auto* draw = ImGui::GetWindowDrawList();
        if (ImGui::IsItemHovered())
        {
            draw->AddRectFilled(position, ImVec2(position.x + size, position.y + size),
                                ImGui::GetColorU32(ImGuiCol_FrameBgHovered), 3.0f);
            ImGui::SetTooltip("%s", tooltip);
        }
        const auto point = [position, size](float x, float y)
        {
            return ImVec2(position.x + size * x, position.y + size * y);
        };
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        if (action == PropertyAction::Find)
        {
            draw->AddCircle(point(0.42f, 0.42f), size * 0.20f, color, 16, 1.5f);
            draw->AddLine(point(0.58f, 0.58f), point(0.80f, 0.80f), color, 1.5f);
        }
        else if (action == PropertyAction::Clear)
        {
            draw->AddLine(point(0.3f, 0.3f), point(0.7f, 0.7f), color, 1.5f);
            draw->AddLine(point(0.7f, 0.3f), point(0.3f, 0.7f), color, 1.5f);
        }
        else
        {
            draw->PathArcTo(point(0.5f, 0.5f), size * 0.28f, -1.6f, 3.0f, 16);
            draw->PathStroke(color, 0, 1.5f);
            draw->AddTriangleFilled(point(0.17f, 0.46f), point(0.39f, 0.45f), point(0.24f, 0.66f), color);
            if (action == PropertyAction::Use)
            {
                draw->AddLine(point(0.38f, 0.5f), point(0.68f, 0.5f), color, 1.5f);
                draw->AddTriangleFilled(point(0.67f, 0.38f), point(0.67f, 0.62f), point(0.82f, 0.5f), color);
            }
        }
        return pressed;
    }
} // namespace toy3d
