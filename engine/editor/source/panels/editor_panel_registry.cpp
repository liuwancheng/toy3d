#include "panels/editor_panel_registry.h"

#include <utility>
#include "imgui.h"
#include "logging/logger.h"

namespace toy3d
{
    bool EditorPanelRegistry::add(EditorPanel panel)
    {
        if (frozen_ || panel.id.empty() || panel.title.empty() || panel.window_name.empty() || !panel.draw)
        { TOY_LOG_ERROR("Invalid or late Editor panel registration: {}", panel.id); return false; }
        for (const auto& existing : panels_)
            if (existing.id == panel.id || existing.window_name == panel.window_name)
            { TOY_LOG_ERROR("Duplicate Editor panel registration: {}", panel.id); return false; }
        if (static_cast<bool>(panel.undo) != static_cast<bool>(panel.redo) || ((panel.undo || panel.save) && !panel.focused))
        { TOY_LOG_ERROR("Editor panel history requires undo, redo and focus callbacks: {}", panel.id); return false; }
        if (history_target_.empty() && (panel.undo || panel.save)) history_target_ = panel.id;
        panels_.push_back(std::move(panel));
        return true;
    }

    void EditorPanelRegistry::draw()
    {
        for (const auto& panel : panels_) panel.draw();
        for (const auto& panel : panels_)
            if ((panel.undo || panel.save) && panel.focused()) history_target_ = panel.id;
    }

    void EditorPanelRegistry::draw_window_menu() const
    {
        for (const auto& panel : panels_)
            if (ImGui::MenuItem(panel.title.c_str()))
            {
                if (panel.open) panel.open();
                else ImGui::SetWindowFocus(panel.window_name.c_str());
            }
    }

    const EditorPanel* EditorPanelRegistry::history_target() const
    {
        for (const auto& panel : panels_)
            if (panel.id == history_target_) return &panel;
        return nullptr;
    }

    void EditorPanelRegistry::undo() { if (const auto* target = history_target()) { if (target->undo) target->undo(); } }
    void EditorPanelRegistry::redo() { if (const auto* target = history_target()) { if (target->redo) target->redo(); } }
    void EditorPanelRegistry::save() { if (const auto* target = history_target()) { if (target->save) target->save(); } }
    void EditorPanelRegistry::process_shortcuts(bool blocked)
    {
        const auto& io = ImGui::GetIO();
        if (blocked || !io.KeyCtrl || io.WantTextInput || ImGui::IsAnyItemActive() ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
        if (ImGui::IsKeyPressed(ImGuiKey_S)) save();
        else if (ImGui::IsKeyPressed(ImGuiKey_Z)) { if (io.KeyShift) redo(); else undo(); }
        else if (ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
    }
    void EditorPanelRegistry::clear() { panels_.clear(); history_target_.clear(); frozen_ = false; }
}
