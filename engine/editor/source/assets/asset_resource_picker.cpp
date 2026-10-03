#include "assets/asset_resource_picker.h"

#include <algorithm>
#include <cstring>

#include "imgui.h"
#include "assets/thumbnails/thumbnail_widget.h"
#include "panels/property_widgets.h"
#include "scene/placement/asset_placement.h"
#include "scene/material_assignments.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        std::string resource_name(const std::string& path)
        {
            const auto slash = path.find_last_of('/');
            auto name = path.substr(slash == std::string::npos ? 0 : slash + 1);
            const auto extension = name.find_last_of('.');
            return extension == std::string::npos ? name : name.substr(0, extension);
        }
    } // namespace

    AssetResourcePicker::AssetResourcePicker(AssetThumbnailPool& thumbnails) : thumbnails_(thumbnails)
    {
    }
    void AssetResourcePicker::set_browse(std::function<void(const AssetId&)> browse)
    {
        browse_ = std::move(browse);
    }
    void AssetResourcePicker::set_builtin_resolver(std::function<StaticMeshRef(const std::string&)> resolver)
    {
        builtin_resolver_ = std::move(resolver);
    }
    void AssetResourcePicker::clear()
    {
        searches_.clear();
        builtins_.clear();
    }
    bool AssetResourcePicker::draw(const char* label, const EditorWorkspace& workspace,
                                   const AssetResourceSelection& current, const std::vector<std::string>& types,
                                   AssetResourceSelection& selected, std::string& error,
                                   const std::function<bool(const AssetCatalogEntry&)>& filter, bool allow_builtins,
                                   bool clear_allowed)
    {
        bool changed = false;
        const auto accepted = [&](const AssetCatalogEntry& entry)
        {
            return std::find(types.begin(), types.end(), entry.file.root_type) != types.end() &&
                   (!filter || filter(entry));
        };
        const auto find = [&](const AssetId& id) -> const AssetCatalogEntry*
        {
            for (const auto& entry : workspace.catalog().entries)
            {
                if (entry.file.asset_id == id)
                {
                    return &entry;
                }
            }
            return nullptr;
        };
        const auto builtin_thumbnail = [&](const std::string& name)
        {
            auto& geometry = builtins_[name];
            if (!geometry && builtin_resolver_)
            {
                geometry = builtin_resolver_(name);
            }
            return thumbnails_.request_builtin_mesh(name, geometry);
        };
        ImGui::PushID(label);
        bool open_picker = false;
        const auto* asset = find(current.asset);
        if (begin_property_row(label))
        {
            const float frame = ImGui::GetFrameHeight();
            const float size = frame * 2.0f + ImGui::GetStyle().ItemSpacing.y;
            const float width = ImGui::GetContentRegionAvail().x;
            const std::string type = asset                      ? asset->file.root_type
                                     : !current.builtin.empty() ? "toy3d.StaticMeshAssetData"
                                     : types.empty()            ? ""
                                                                : types.front();
            ImGui::BeginGroup();
            if (asset_thumbnail_widget(asset                      ? thumbnails_.request(*asset)
                                       : !current.builtin.empty() ? builtin_thumbnail(current.builtin)
                                                                  : AssetThumbnailView{},
                                       type, size, true) &&
                !ImGui::GetDragDropPayload())
            {
                open_picker = true;
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            const std::string name = asset                      ? resource_name(asset->path.utf8())
                                     : !current.builtin.empty() ? current.builtin
                                     : current.asset.valid()    ? "Missing resource"
                                                                : "None";
            const float name_width = std::max(1.0f, width - size - ImGui::GetStyle().ItemSpacing.x);
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(18, 18, 19, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
            const auto name_position = ImGui::GetCursorScreenPos();
            if (ImGui::Button((name + "###Resource").c_str(), ImVec2(name_width, frame)) &&
                !ImGui::GetDragDropPayload())
            {
                open_picker = true;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            if (asset && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", asset->path.utf8().c_str());
            }
            auto* draw = ImGui::GetWindowDrawList();
            const float arrow_x = name_position.x + name_width - frame * 0.55f;
            draw->AddRectFilled(ImVec2(name_position.x + std::max(0.0f, name_width - frame), name_position.y),
                                ImVec2(name_position.x + name_width, name_position.y + frame),
                                IM_COL32(18, 18, 19, 255), 2.0f);
            draw->AddTriangleFilled(ImVec2(arrow_x - 3, name_position.y + frame * 0.4f),
                                    ImVec2(arrow_x + 3, name_position.y + frame * 0.4f),
                                    ImVec2(arrow_x, name_position.y + frame * 0.65f),
                                    ImGui::GetColorU32(ImGuiCol_Text));
            // The action row flows normally below the resource name, inside the card group.
            if (selected_asset_)
            {
                const auto* entry = find(selected_asset_());
                ImGui::BeginDisabled(!entry || !accepted(*entry));
                if (property_action_button("Use", PropertyAction::Use,
                                           "Use compatible asset selected in the Content Browser"))
                {
                    selected = {entry->file.asset_id, {}};
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine(0, 2.0f);
            }
            ImGui::BeginDisabled(!browse_ || !current.asset.valid());
            if (property_action_button("Find", PropertyAction::Find, "Find resource in the Content Browser"))
            {
                browse_(current.asset);
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2.0f);
            ImGui::BeginDisabled(!clear_allowed || (!current.asset.valid() && current.builtin.empty()));
            if (property_action_button("Clear", PropertyAction::Clear, "Clear reference / restore inherited default"))
            {
                selected = {};
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::EndGroup();
            ImGui::EndGroup();
            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ASSET_DRAG_PAYLOAD);
                if (!payload)
                {
                    payload = ImGui::AcceptDragDropPayload(MATERIAL_ASSET_DRAG_PAYLOAD);
                }
                if (!payload)
                {
                    payload = ImGui::AcceptDragDropPayload("TOY3D_TEXTURE_ASSET");
                }
                if (payload && payload->IsDelivery())
                {
                    AssetId id;
                    if (payload->DataSize == sizeof(id))
                    {
                        std::memcpy(&id, payload->Data, sizeof(id));
                    }
                    const auto* entry = find(id);
                    if (entry && accepted(*entry))
                    {
                        selected = {id, {}};
                        changed = true;
                    }
                    else
                    {
                        error = "The dragged resource is missing or incompatible with this field.";
                    }
                }
                ImGui::EndDragDropTarget();
            }
            end_property_row();
        }
        if (open_picker)
        {
            ImGui::OpenPopup("Resource picker");
        }
        ImGui::SetNextWindowSize(ImVec2(410, 460), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("Resource picker"))
        {
            auto& search = searches_[ImGui::GetID("Search")];
            ImGui::InputText("Search", search.data(), search.size());
            if (clear_allowed && ImGui::Selectable("None", !current.asset.valid() && current.builtin.empty()))
            {
                selected = {};
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (allow_builtins)
            {
                for (const auto* builtin : {"Cube", "Plane"})
                {
                    if (search[0] && std::string(builtin).find(search.data()) == std::string::npos)
                    {
                        continue;
                    }
                    asset_thumbnail_widget(builtin_thumbnail(builtin), "toy3d.StaticMeshAssetData", 40);
                    ImGui::SameLine();
                    if (ImGui::Selectable(builtin, current.builtin == builtin, 0, ImVec2(0, 40)))
                    {
                        selected = {{}, builtin};
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::BeginChild("Assets", ImVec2(0, 0));
            ImGuiListClipper clipper;
            std::vector<const AssetCatalogEntry*> matches;
            for (const auto& entry : workspace.catalog().entries)
            {
                if (accepted(entry) &&
                    (!search[0] || resource_name(entry.path.utf8()).find(search.data()) != std::string::npos))
                {
                    matches.push_back(&entry);
                }
            }
            clipper.Begin(static_cast<int>(matches.size()), 44);
            while (clipper.Step())
            {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                {
                    const auto& entry = *matches[static_cast<std::size_t>(index)];
                    ImGui::PushID(entry.file.asset_id.hex().c_str());
                    asset_thumbnail_widget(thumbnails_.request(entry), entry.file.root_type, 40);
                    ImGui::SameLine();
                    if (ImGui::Selectable(resource_name(entry.path.utf8()).c_str(),
                                          current.asset == entry.file.asset_id, 0, ImVec2(0, 40)))
                    {
                        selected = {entry.file.asset_id, {}};
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", entry.path.utf8().c_str());
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }
} // namespace toy3d
