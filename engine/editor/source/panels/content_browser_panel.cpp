#include "panels/content_browser_panel.h"
#include "scene/material_assignments.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "imgui.h"
#include "logging/logger.h"
#include "scene/placement/asset_placement.h"
#include "scene/editor_selection.h"
#include "assets/thumbnails/asset_thumbnail_pool.h"
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
                draw.AddCircleFilled(ImVec2(center.x - size * 0.07f, center.y - size * 0.07f), size * 0.21f,
                                     IM_COL32(146, 195, 208, 255), 32);
                if (type == "toy3d.MaterialInstanceAssetData")
                {
                    draw.AddText(ImVec2(position.x + size * 0.7f, position.y + size * 0.7f), IM_COL32_WHITE, "MI");
                }
            }
            else if (type == "toy3d.Texture2DAssetData")
            {
                draw.AddRectFilled(a, b, IM_COL32(124, 151, 173, 255), 3);
                draw.AddRectFilled(ImVec2(a.x + 4, a.y + 4), ImVec2(b.x - 4, b.y - 4), IM_COL32(62, 85, 103, 255), 2);
                draw.AddCircleFilled(ImVec2(a.x + size * 0.17f, a.y + size * 0.16f), size * 0.055f,
                                     IM_COL32(232, 205, 128, 255));
                draw.AddTriangleFilled(ImVec2(a.x + 4, b.y - 4), ImVec2(a.x + size * 0.24f, a.y + size * 0.22f),
                                       ImVec2(a.x + size * 0.48f, b.y - 4), IM_COL32(122, 177, 143, 255));
            }
            else if (type == "toy3d.SceneAssetData")
            {
                draw.AddRectFilled(a, b, IM_COL32(57, 82, 105, 255), 4);
                draw.AddCircleFilled(ImVec2(a.x + size * 0.14f, a.y + size * 0.14f), size * 0.065f,
                                     IM_COL32(245, 199, 103, 255));
                draw.AddTriangleFilled(ImVec2(a.x + 4, b.y - 4), ImVec2(a.x + size * 0.25f, a.y + size * 0.2f),
                                       ImVec2(a.x + size * 0.47f, b.y - 4), IM_COL32(115, 163, 130, 255));
                draw.AddTriangleFilled(ImVec2(a.x + size * 0.25f, b.y - 4),
                                       ImVec2(a.x + size * 0.47f, a.y + size * 0.12f), ImVec2(b.x - 4, b.y - 4),
                                       IM_COL32(150, 185, 150, 255));
            }
            else
            {
                const ImVec2 top(position.x + size * 0.5f, position.y + size * 0.18f);
                const ImVec2 left(a.x, position.y + size * 0.4f);
                const ImVec2 right(b.x, left.y);
                const ImVec2 middle(top.x, position.y + size * 0.58f);
                const ImVec2 bottom(top.x, position.y + size * 0.85f);
                draw.AddQuadFilled(top, right, middle, left, color);
                draw.AddQuadFilled(left, middle, bottom, ImVec2(left.x, bottom.y - size * 0.18f),
                                   IM_COL32(72, 103, 134, 255));
                draw.AddQuadFilled(middle, right, ImVec2(right.x, bottom.y - size * 0.18f), bottom,
                                   IM_COL32(91, 127, 159, 255));
            }
        }

        struct BrowserItem
        {
            std::string path;
            const AssetCatalogEntry* asset = nullptr;
        };
    } // namespace

    void ContentBrowserPanel::clear()
    {
        pending_delete_ = {};
        delete_error_.clear();
    }

    ContentBrowserActions ContentBrowserPanel::draw(EditorWorkspace& workspace, EditorSelection& selection,
                                                    std::string& folder, bool& show_engine_content,
                                                    AssetThumbnailPool& thumbnails, bool import_enabled)
    {
        constexpr float tile_size = 128.0f;
        AssetId requested_delete;
        ContentBrowserActions actions;
        if (ImGui::Begin("Content Browser"))
        {
            const ImVec2 region = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            actions.visible = true;
            actions.region_min = Vector2(region.x, region.y);
            actions.region_max = Vector2(region.x + size.x, region.y + size.y);
            const bool writable =
                workspace.has_project() && (folder == "/Project" || folder.compare(0, 9, "/Project/") == 0);
            if (ImGui::Button("Rescan Assets"))
            {
                if (!workspace.refresh())
                {
                    TOY_LOG_ERROR("Content Browser refresh failed: {}", workspace.error());
                }
                else
                {
                    thumbnails.invalidate();
                    actions.assets_refreshed = true;
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Rescan Project and Engine asset files. This does not reimport source files.");
            }
            ImGui::SameLine();
            if (!workspace.has_project())
            {
                show_engine_content = true;
            }
            ImGui::BeginDisabled(!workspace.has_project());
            if (ImGui::Checkbox("Show Engine Content", &show_engine_content) && !show_engine_content &&
                (folder == "/Engine" || folder.compare(0, 8, "/Engine/") == 0))
            {
                folder = workspace.has_project() ? "/Project" : "/Engine";
            }
            ImGui::EndDisabled();
            ImGui::TextUnformatted(folder.c_str());
            if (folder == "/Engine" || folder.compare(0, 8, "/Engine/") == 0)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(read only)");
            }
            if (!workspace.error().empty())
            {
                ImGui::TextWrapped("Asset scan: %s", workspace.error().c_str());
            }
            if (!delete_error_.empty())
            {
                ImGui::TextWrapped("Asset operation: %s", delete_error_.c_str());
            }
            ImGui::Separator();
            if (ImGui::BeginTable("Content Browser Columns", 2,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 190);
                ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("Folder Tree");
                for (const auto& directory : workspace.catalog().directories)
                {
                    const auto& path = directory.utf8();
                    if (!show_engine_content && (path == "/Engine" || path.compare(0, 8, "/Engine/") == 0))
                    {
                        continue;
                    }
                    const auto depth = std::count(path.begin(), path.end(), '/');
                    const float indent = static_cast<float>(depth > 0 ? depth - 1 : 0) * 12;
                    ImGui::Indent(indent);
                    ImGui::PushID(path.c_str());
                    if (ImGui::Selectable(path.substr(path.find_last_of('/') + 1).c_str(), folder == path))
                    {
                        folder = path;
                    }
                    ImGui::PopID();
                    ImGui::Unindent(indent);
                }
                ImGui::EndChild();
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("Asset Tiles");
                std::vector<BrowserItem> items;
                for (const auto& directory : workspace.catalog().directories)
                {
                    if (parent_folder(directory.utf8()) == folder)
                    {
                        items.push_back({directory.utf8(), nullptr});
                    }
                }
                for (const auto& asset : workspace.catalog().entries)
                {
                    if (parent_folder(asset.path.utf8()) == folder)
                    {
                        items.push_back({asset.path.utf8(), &asset});
                    }
                }
                const float spacing = 12;
                // Three caption lines keep ordinary asset names readable at the smallest tile size.
                // The existing tooltip remains the full-name fallback for unusually long names.
                const float row_height = tile_size + 64;
                const int columns =
                    std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (tile_size + spacing)));
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
                            if (index >= items.size())
                            {
                                break;
                            }
                            const auto& item = items[index];
                            const ImVec2 position(origin.x + column * (tile_size + spacing),
                                                  origin.y + row * row_height);
                            const ImVec2 end(position.x + tile_size, position.y + row_height - spacing);
                            ImGui::SetCursorScreenPos(position);
                            ImGui::PushID(item.path.c_str());
                            const bool selected = item.asset && selection.asset_id() == item.asset->file.asset_id;
                            if (ImGui::InvisibleButton("Tile", ImVec2(tile_size, row_height - spacing)))
                            {
                                if (item.asset)
                                {
                                    selection.select_asset(item.asset->file.asset_id);
                                    if (item.asset->file.root_type == "toy3d.Texture2DAssetData")
                                    {
                                        actions.asset_open = item.asset->file.asset_id;
                                    }
                                }
                                else
                                {
                                    folder = item.path;
                                }
                            }
                            const bool hovered = ImGui::IsItemHovered();
                            const bool is_material =
                                item.asset && (item.asset->file.root_type == "toy3d.MaterialAssetData" ||
                                               item.asset->file.root_type == "toy3d.MaterialInstanceAssetData");
                            if (item.asset && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                actions.asset_open = item.asset->file.asset_id;
                                actions.asset_focus = true;
                            }
                            if (item.asset &&
                                (is_material || item.asset->file.root_type == "toy3d.StaticMeshAssetData") &&
                                ImGui::BeginDragDropSource())
                            {
                                const AssetId id = item.asset->file.asset_id;
                                ImGui::SetDragDropPayload(
                                    is_material ? MATERIAL_ASSET_DRAG_PAYLOAD : ASSET_DRAG_PAYLOAD, &id, sizeof(id));
                                ImGui::Text("%s: %s", is_material ? "Assign Material" : "Place Static Mesh",
                                            item.path.c_str());
                                ImGui::EndDragDropSource();
                            }
                            if (item.asset && item.asset->file.root_type == "toy3d.Texture2DAssetData" &&
                                ImGui::BeginDragDropSource())
                            {
                                const AssetId id = item.asset->file.asset_id;
                                ImGui::SetDragDropPayload("TOY3D_TEXTURE_ASSET", &id, sizeof(id));
                                ImGui::Text("Assign Texture2D: %s", item.path.c_str());
                                ImGui::EndDragDropSource();
                            }
                            auto& draw = *ImGui::GetWindowDrawList();
                            draw.AddRectFilled(position, end,
                                               selected  ? IM_COL32(48, 89, 126, 255)
                                               : hovered ? IM_COL32(55, 58, 63, 255)
                                                         : IM_COL32(31, 33, 37, 255),
                                               4);
                            AssetThumbnailView thumbnail;
                            if (item.asset)
                            {
                                thumbnail = thumbnails.request(*item.asset);
                            }
                            if (thumbnail.texture_id.valid())
                            {
                                draw.AddImage(reinterpret_cast<ImTextureID>(
                                                  static_cast<std::uintptr_t>(thumbnail.texture_id.value())),
                                              position, ImVec2(position.x + tile_size, position.y + tile_size));
                            }
                            else
                            {
                                draw_placeholder(draw, position, tile_size, !item.asset,
                                                 item.asset ? item.asset->file.root_type : "");
                            }
                            const std::string name = item.path.substr(item.path.find_last_of('/') + 1);
                            draw.PushClipRect(position, end, true);
                            draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                                         ImVec2(position.x + 4, position.y + tile_size + 3), IM_COL32_WHITE,
                                         name.c_str(), nullptr, tile_size - 8);
                            if (thumbnail.busy)
                            {
                                draw.AddText(ImVec2(position.x + 4, position.y + tile_size - 20), IM_COL32_WHITE,
                                             "Generating...");
                            }
                            if (!thumbnail.error.empty())
                            {
                                draw.AddText(ImVec2(position.x + 4, position.y + tile_size - 20),
                                             IM_COL32(255, 130, 100, 255), "Failed");
                            }
                            draw.PopClipRect();
                            if (hovered)
                            {
                                ImGui::BeginTooltip();
                                ImGui::TextUnformatted(name.c_str());
                                if (item.asset)
                                {
                                    ImGui::TextDisabled("%s", item.asset->file.root_type.c_str());
                                }
                                if (!thumbnail.error.empty())
                                {
                                    ImGui::TextWrapped("%s", thumbnail.error.c_str());
                                }
                                ImGui::EndTooltip();
                            }
                            if (item.asset && ImGui::BeginPopupContextItem("Asset Actions"))
                            {
                                if (ImGui::MenuItem("Open"))
                                {
                                    actions.asset_open = item.asset->file.asset_id;
                                    actions.asset_focus = true;
                                }
                                if (is_material && ImGui::MenuItem("Create Material Instance..."))
                                {
                                    actions.material_creation_requested = true;
                                    actions.material_creation_kind = MaterialAssetCreationKind::MaterialInstance;
                                    actions.material_parent = item.asset->file.asset_id;
                                }
                                const bool is_mesh = item.asset->file.root_type == "toy3d.StaticMeshAssetData";
                                const bool writable = item.path.compare(0, 9, "/Project/") == 0;
                                if (ImGui::MenuItem("Generate / Regenerate Thumbnail", nullptr, false, is_mesh))
                                {
                                    thumbnails.generate(item.asset->file.asset_id);
                                }
                                if (ImGui::MenuItem("Delete Asset...", nullptr, false, writable))
                                {
                                    requested_delete = item.asset->file.asset_id;
                                }
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
                if (items.empty())
                {
                    ImGui::TextDisabled("Right-click here to import or create assets");
                }
                if (ImGui::BeginPopupContextWindow("Content Actions",
                                                   ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
                {
                    if (ImGui::MenuItem("Import Static Mesh...", nullptr, false, import_enabled && writable))
                    {
                        actions.import_requested = true;
                    }
                    if (ImGui::MenuItem("Import Texture2D...", nullptr, false, writable))
                    {
                        actions.texture_import_requested = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Create Material...", nullptr, false, writable))
                    {
                        actions.material_creation_requested = true;
                        actions.material_creation_kind = MaterialAssetCreationKind::Material;
                    }
                    if (ImGui::MenuItem("Create Material Instance...", nullptr, false, writable))
                    {
                        actions.material_creation_requested = true;
                        actions.material_creation_kind = MaterialAssetCreationKind::MaterialInstance;
                    }
                    if (!writable)
                    {
                        ImGui::TextDisabled("Engine content is read only");
                    }
                    if (!import_enabled)
                    {
                        ImGui::TextDisabled("Model import is disabled in this build");
                    }
                    ImGui::EndPopup();
                }
                ImGui::EndChild();
                ImGui::EndTable();
            }
            if (requested_delete.valid())
            {
                pending_delete_ = requested_delete;
                delete_error_.clear();
                ImGui::OpenPopup("Delete Asset");
            }
            if (ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                const auto* location = workspace.catalog().index.find(pending_delete_);
                ImGui::TextWrapped("Delete %s and its paired data?",
                                   location ? location->path.utf8().c_str() : "the selected asset");
                if (ImGui::Button("Delete"))
                {
                    const AssetStatus deleted = workspace.delete_asset(pending_delete_);
                    if (deleted.succeeded())
                    {
                        if (selection.asset_id() == pending_delete_)
                        {
                            selection.clear_asset();
                        }
                        thumbnails.invalidate();
                        pending_delete_ = {};
                        delete_error_.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        delete_error_ = deleted.message;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    pending_delete_ = {};
                    delete_error_.clear();
                    ImGui::CloseCurrentPopup();
                }
                if (!delete_error_.empty())
                {
                    ImGui::TextWrapped("%s", delete_error_.c_str());
                }
                ImGui::EndPopup();
            }
        }
        ImGui::End();
        return actions;
    }
} // namespace toy3d
