#include "assets/asset_resource_picker.h"

#include <algorithm>
#include <cstring>

#include "imgui.h"
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
        void draw_thumbnail(const AssetThumbnailView& thumbnail, float size)
        {
            const auto position = ImGui::GetCursorScreenPos();
            if (thumbnail.texture_id.valid())
            {
                ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(thumbnail.texture_id.value())),
                             ImVec2(size, size));
            }
            else
            {
                ImGui::Dummy(ImVec2(size, size));
                auto* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(position, ImVec2(position.x + size, position.y + size), IM_COL32(45, 48, 52, 255),
                                    4);
                draw->AddText(ImVec2(position.x + 5, position.y + size * 0.4f), IM_COL32(170, 175, 180, 255),
                              thumbnail.busy ? "..." : "--");
            }
            if (!thumbnail.error.empty() && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", thumbnail.error.c_str());
            }
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
                                   const std::function<bool(const AssetCatalogEntry&)>& filter, bool allow_builtins)
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
        ImGui::TextUnformatted(label);
        const auto* asset = find(current.asset);
        draw_thumbnail(asset                      ? thumbnails_.request(*asset)
                       : !current.builtin.empty() ? builtin_thumbnail(current.builtin)
                                                  : AssetThumbnailView{},
                       48);
        ImGui::SameLine();
        const std::string name = asset                      ? resource_name(asset->path.utf8())
                                 : !current.builtin.empty() ? current.builtin
                                 : current.asset.valid()    ? "Missing resource"
                                                            : "None";
        if (ImGui::Button((name + "###Resource").c_str(),
                          ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 105), 48)))
        {
            ImGui::OpenPopup("Resource picker");
        }
        if (asset && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", asset->path.utf8().c_str());
        }
        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ASSET_DRAG_PAYLOAD);
            if (!payload)
            {
                payload = ImGui::AcceptDragDropPayload(MATERIAL_ASSET_DRAG_PAYLOAD);
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
        ImGui::SameLine();
        ImGui::BeginDisabled(!current.asset.valid() && current.builtin.empty());
        if (ImGui::Button("Clear"))
        {
            selected = {};
            changed = true;
        }
        ImGui::EndDisabled();
        if (browse_ && current.asset.valid())
        {
            ImGui::SameLine();
            if (ImGui::Button("Find"))
            {
                browse_(current.asset);
            }
        }
        ImGui::SetNextWindowSize(ImVec2(410, 460), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("Resource picker"))
        {
            auto& search = searches_[ImGui::GetID("Search")];
            ImGui::InputText("Search", search.data(), search.size());
            if (ImGui::Selectable("None", !current.asset.valid() && current.builtin.empty()))
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
                    draw_thumbnail(builtin_thumbnail(builtin), 40);
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
                    draw_thumbnail(thumbnails_.request(entry), 40);
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
