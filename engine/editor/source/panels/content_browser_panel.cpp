#include "panels/content_browser_panel.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "imgui.h"
#include "logging/logger.h"
#include "placement/asset_placement.h"
#include "selection/editor_selection.h"
#include "thumbnails/asset_thumbnail_pool.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        std::string parent_folder(const std::string& path)
        {
            return path.substr(0, path.find_last_of('/'));
        }

        void draw_placeholder(ImDrawList& draw, ImVec2 position, float size, bool folder, const std::string& type)
        {
            const ImU32 color = folder ? IM_COL32(190, 151, 74, 255) : IM_COL32(116, 149, 181, 255);
            const ImVec2 a(position.x + size * 0.2f, position.y + size * 0.3f);
            const ImVec2 b(position.x + size * 0.8f, position.y + size * 0.75f);
            if (folder)
            {
                draw.AddRectFilled(a, b, color, 4);
                draw.AddRectFilled(ImVec2(a.x, a.y - size * 0.09f), ImVec2(a.x + size * 0.25f, a.y + 4), color, 3);
            }
            else if (type == "toy3d.MaterialAssetData" || type == "toy3d.MaterialInstanceAssetData")
            {
                const ImVec2 center(position.x + size * 0.5f, position.y + size * 0.5f);
                draw.AddCircleFilled(center, size * 0.3f, IM_COL32(106, 151, 167, 255), 32);
                draw.AddCircleFilled(ImVec2(center.x - size * 0.07f, center.y - size * 0.07f),
                    size * 0.21f, IM_COL32(146, 195, 208, 255), 32);
                if (type == "toy3d.MaterialInstanceAssetData")
                    draw.AddText(ImVec2(position.x + size * 0.7f, position.y + size * 0.7f), IM_COL32_WHITE, "MI");
            }
            else
            {
                const ImVec2 top(position.x + size * 0.5f, position.y + size * 0.18f);
                const ImVec2 left(a.x, position.y + size * 0.4f);
                const ImVec2 right(b.x, left.y);
                const ImVec2 middle(top.x, position.y + size * 0.58f);
                const ImVec2 bottom(top.x, position.y + size * 0.85f);
                draw.AddQuadFilled(top, right, middle, left, color);
                draw.AddQuadFilled(left, middle, bottom, ImVec2(left.x, bottom.y - size * 0.18f), IM_COL32(72, 103, 134, 255));
                draw.AddQuadFilled(middle, right, ImVec2(right.x, bottom.y - size * 0.18f), bottom, IM_COL32(91, 127, 159, 255));
            }
        }

        struct BrowserItem
        {
            std::string path;
            const AssetCatalogEntry* asset = nullptr;
        };
    }

    ContentBrowserActions draw_content_browser(EditorWorkspace& workspace, EditorSelection& selection, std::string& folder,
                              bool& show_engine_content, AssetThumbnailPool& thumbnails, float& tile_size, bool import_enabled)
    {
        ContentBrowserActions actions;
        if (ImGui::Begin("Content Browser"))
        {
            const ImVec2 region = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            actions.visible = true;
            actions.region_min = Vector2(region.x, region.y);
            actions.region_max = Vector2(region.x + size.x, region.y + size.y);
            const bool writable = folder == "/Project" || folder.compare(0, 9, "/Project/") == 0;
            if (ImGui::Button("Refresh"))
            {
                if (!workspace.refresh()) TOY_LOG_ERROR("Content Browser refresh failed: {}", workspace.error());
                else thumbnails.invalidate();
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Show Engine Content", &show_engine_content) && !show_engine_content &&
                (folder == "/Engine" || folder.compare(0, 8, "/Engine/") == 0)) folder = "/Project";
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::SliderFloat("Tile Size", &tile_size, 96, 160, "%.0f");
            ImGui::TextUnformatted(folder.c_str());
            if (folder == "/Engine" || folder.compare(0, 8, "/Engine/") == 0)
            { ImGui::SameLine(); ImGui::TextDisabled("(read only)"); }
            if (!workspace.error().empty()) ImGui::TextWrapped("Asset scan: %s", workspace.error().c_str());
            ImGui::Separator();
            if (ImGui::BeginTable("Content Browser Columns", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 190);
                ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("Folder Tree");
                for (const auto& directory : workspace.catalog().directories)
                {
                    const auto& path = directory.utf8();
                    if (!show_engine_content && (path == "/Engine" || path.compare(0, 8, "/Engine/") == 0)) continue;
                    const auto depth = std::count(path.begin(), path.end(), '/');
                    const float indent = static_cast<float>(depth > 0 ? depth - 1 : 0) * 12;
                    ImGui::Indent(indent);
                    ImGui::PushID(path.c_str());
                    if (ImGui::Selectable(path.substr(path.find_last_of('/') + 1).c_str(), folder == path)) folder = path;
                    ImGui::PopID();
                    ImGui::Unindent(indent);
                }
                ImGui::EndChild();
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("Asset Tiles");
                std::vector<BrowserItem> items;
                for (const auto& directory : workspace.catalog().directories)
                    if (parent_folder(directory.utf8()) == folder) items.push_back({directory.utf8(), nullptr});
                for (const auto& asset : workspace.catalog().entries)
                    if (parent_folder(asset.path.utf8()) == folder) items.push_back({asset.path.utf8(), &asset});
                const float spacing = 12;
                const float row_height = tile_size + 48;
                const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (tile_size + spacing)));
                const int rows = static_cast<int>((items.size() + columns - 1) / columns);
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImGuiListClipper clipper;
                clipper.Begin(rows, row_height);
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        for (int column = 0; column < columns; ++column)
                        {
                            const auto index = static_cast<std::size_t>(row * columns + column);
                            if (index >= items.size()) break;
                            const auto& item = items[index];
                            const ImVec2 position(origin.x + column * (tile_size + spacing), origin.y + row * row_height);
                            const ImVec2 end(position.x + tile_size, position.y + row_height - spacing);
                            ImGui::SetCursorScreenPos(position);
                            ImGui::PushID(item.path.c_str());
                            const bool selected = item.asset && selection.asset_id() == item.asset->file.asset_id;
                            if (ImGui::InvisibleButton("Tile", ImVec2(tile_size, row_height - spacing)))
                            {
                                if (item.asset) selection.select_asset(item.asset->file.asset_id);
                                else folder = item.path;
                            }
                            const bool hovered = ImGui::IsItemHovered();
                            if (item.asset && item.asset->file.root_type == "toy3d.StaticMeshAssetData" && ImGui::BeginDragDropSource())
                            {
                                const AssetId id = item.asset->file.asset_id;
                                ImGui::SetDragDropPayload(ASSET_DRAG_PAYLOAD, &id, sizeof(id));
                                ImGui::Text("Place Static Mesh: %s", item.path.c_str());
                                ImGui::EndDragDropSource();
                            }
                            auto& draw = *ImGui::GetWindowDrawList();
                            draw.AddRectFilled(position, end, selected ? IM_COL32(48, 89, 126, 255) :
                                hovered ? IM_COL32(55, 58, 63, 255) : IM_COL32(31, 33, 37, 255), 4);
                            AssetThumbnailView thumbnail;
                            if (item.asset) thumbnail = thumbnails.request(*item.asset);
                            if (thumbnail.texture_id.valid())
                                draw.AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(thumbnail.texture_id.value())),
                                    position, ImVec2(position.x + tile_size, position.y + tile_size));
                            else draw_placeholder(draw, position, tile_size, !item.asset, item.asset ? item.asset->file.root_type : "");
                            const std::string name = item.path.substr(item.path.find_last_of('/') + 1);
                            draw.PushClipRect(position, end, true);
                            draw.AddText(ImVec2(position.x + 4, position.y + tile_size + 3), IM_COL32_WHITE, name.c_str());
                            if (thumbnail.busy) draw.AddText(ImVec2(position.x + 4, position.y + tile_size - 20), IM_COL32_WHITE, "Generating...");
                            if (!thumbnail.error.empty()) draw.AddText(ImVec2(position.x + 4, position.y + tile_size - 20), IM_COL32(255, 130, 100, 255), "Failed");
                            draw.PopClipRect();
                            if (hovered)
                            {
                                ImGui::BeginTooltip();
                                ImGui::TextUnformatted(name.c_str());
                                if (item.asset) ImGui::TextDisabled("%s", item.asset->file.root_type.c_str());
                                if (!thumbnail.error.empty()) ImGui::TextWrapped("%s", thumbnail.error.c_str());
                                ImGui::EndTooltip();
                            }
                            if (item.asset && ImGui::BeginPopupContextItem("Asset Actions"))
                            {
                                if (item.asset->file.root_type == "toy3d.MaterialAssetData" && ImGui::MenuItem("Create Material Instance..."))
                                {
                                    actions.material_creation_requested = true;
                                    actions.material_creation_kind = MaterialAssetCreationKind::MaterialInstance;
                                    actions.material_parent = item.asset->file.asset_id;
                                }
                                const bool is_mesh = item.asset->file.root_type == "toy3d.StaticMeshAssetData";
                                const bool writable = item.path.compare(0, 9, "/Project/") == 0;
                                if (ImGui::MenuItem(writable ? "Generate / Regenerate Thumbnail" : "Generate Thumbnail (memory only)", nullptr, false, is_mesh))
                                    thumbnails.generate(item.asset->file.asset_id, writable);
                                ImGui::EndPopup();
                            }
                            ImGui::PopID();
                        }
                        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + (row + 1) * row_height));
                        // Submit an item after advancing the cursor so ImGui
                        // validates the row extent and the child scroll range.
                        ImGui::Dummy(ImVec2(0, 0));
                    }
                }
                if (items.empty()) ImGui::TextDisabled("Right-click here to import or create assets");
                if (ImGui::BeginPopupContextWindow("Content Actions", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
                {
                    if (ImGui::MenuItem("Import...", nullptr, false, import_enabled && writable)) actions.import_requested = true;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Material...", nullptr, false, writable))
                    {
                        actions.material_creation_requested = true;
                        actions.material_creation_kind = MaterialAssetCreationKind::Material;
                    }
                    if (ImGui::MenuItem("Material Instance...", nullptr, false, writable))
                    {
                        actions.material_creation_requested = true;
                        actions.material_creation_kind = MaterialAssetCreationKind::MaterialInstance;
                    }
                    if (!writable) ImGui::TextDisabled("Engine content is read only");
                    if (!import_enabled) ImGui::TextDisabled("Model import is disabled in this build");
                    ImGui::EndPopup();
                }
                ImGui::EndChild();
                ImGui::EndTable();
            }
        }
        ImGui::End();
        return actions;
    }
}
