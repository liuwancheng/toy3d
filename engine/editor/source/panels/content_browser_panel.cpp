#include "panels/content_browser_panel.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <vector>

#include "imgui.h"
#include "logging/logger.h"
#include "panels/property_widgets.h"
#include "scene/placement/asset_placement.h"
#include "scene/editor_selection.h"
#include "scene/material_assignments.h"
#include "assets/thumbnails/asset_thumbnail_pool.h"
#include "assets/thumbnails/thumbnail_widget.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        std::string parent_folder(const std::string& path)
        {
            return path.substr(0, path.find_last_of('/'));
        }

        std::string folder_label(const std::string& path)
        {
            return path == "/Project"  ? "Content"
                   : path == "/Engine" ? "Engine Content"
                                       : path.substr(path.find_last_of('/') + 1);
        }

        void toolbar_divider(float height)
        {
            const auto position = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(1.0f, height));
            ImGui::GetWindowDrawList()->AddLine(ImVec2(position.x, position.y + height * 0.2f),
                                                ImVec2(position.x, position.y + height * 0.8f),
                                                ImGui::GetColorU32(ImGuiCol_Separator));
        }

        struct BrowserItem
        {
            std::string path;
            const AssetCatalogEntry* asset = nullptr;
        };

        std::string item_name(const BrowserItem& item)
        {
            std::string name = item.path.substr(item.path.find_last_of('/') + 1);
            if (item.asset)
            {
                name = name.substr(0, name.find_last_of('.'));
            }
            return name;
        }

        const char* asset_label(const std::string& type)
        {
            if (type == "toy3d.SceneAssetData")
            {
                return "Level";
            }
            if (type == "toy3d.StaticMeshAssetData")
            {
                return "Static Mesh";
            }
            if (type == "toy3d.SkeletalMeshAssetData")
            {
                return "Skeletal Mesh";
            }
            if (type == "toy3d.MaterialAssetData")
            {
                return "Material";
            }
            if (type == "toy3d.MaterialInstanceAssetData")
            {
                return "Material Instance";
            }
            if (type == "toy3d.Texture2DAssetData")
            {
                return "Texture";
            }
            if (type == "toy3d.SkeletonAssetData")
            {
                return "Skeleton";
            }
            if (type == "toy3d.AnimationSequenceAssetData")
            {
                return "Animation Sequence";
            }
            if (type == "toy3d.EnvironmentAssetData")
            {
                return "Environment";
            }
            return "Asset";
        }

        bool matches_search(std::string name, std::string search)
        {
            const auto lower = [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            };
            std::transform(name.begin(), name.end(), name.begin(), lower);
            std::transform(search.begin(), search.end(), search.begin(), lower);
            return name.find(search) != std::string::npos;
        }

        void draw_create_menu(ContentBrowserActions& actions, bool writable, const std::string& folder);

        void draw_folder_tree(const AssetCatalog& catalog, const std::string& path, std::string& folder, bool reveal,
                              ContentBrowserActions& actions)
        {
            const bool has_children = std::any_of(catalog.directories.begin(), catalog.directories.end(),
                                                  [&path](const VirtualPath& directory)
                                                  {
                                                      return parent_folder(directory.utf8()) == path;
                                                  });
            const bool ancestor = folder == path || folder.compare(0, path.size() + 1, path + "/") == 0;
            if (reveal && ancestor)
            {
                ImGui::SetNextItemOpen(true);
            }
            auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!has_children)
            {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (folder == path)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            const std::string label = folder_label(path);
            const bool open = ImGui::TreeNodeEx(path.c_str(), flags, "%s", label.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                folder = path;
            }
            if (ImGui::BeginPopupContextItem())
            {
                folder = path;
                draw_create_menu(actions, path == "/Project" || path.compare(0, 9, "/Project/") == 0, path);
                ImGui::EndPopup();
            }
            if (open)
            {
                for (const auto& directory : catalog.directories)
                {
                    if (parent_folder(directory.utf8()) == path)
                    {
                        draw_folder_tree(catalog, directory.utf8(), folder, reveal, actions);
                    }
                }
                ImGui::TreePop();
            }
        }

        void draw_create_menu(ContentBrowserActions& actions, bool writable, const std::string& folder)
        {
            const std::string import_label = "Import to " + folder + "...";
            if (ImGui::MenuItem(import_label.c_str(), nullptr, false, writable))
            {
                actions.import_requested = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Material", nullptr, false, writable))
            {
                actions.material_creation_requested = true;
                actions.material_creation_kind = MaterialAssetCreationKind::Material;
            }
            if (ImGui::MenuItem("Material Instance", nullptr, false, writable))
            {
                actions.material_creation_requested = true;
                actions.material_creation_kind = MaterialAssetCreationKind::MaterialInstance;
            }
        }
    } // namespace

    void ContentBrowserPanel::clear()
    {
        pending_delete_ = {};
        delete_error_.clear();
        search_.fill('\0');
        revealed_folder_.clear();
    }

    ContentBrowserActions ContentBrowserPanel::draw(EditorWorkspace& workspace, EditorSelection& selection,
                                                    std::string& folder, bool& show_engine_content,
                                                    AssetThumbnailPool& thumbnails, bool import_enabled)
    {
        const float requested_tile_size = ImGui::GetFontSize() * 5.0f * thumbnail_scale_;
        AssetId requested_delete;
        ContentBrowserActions actions;
        if (ImGui::Begin("Content Browser"))
        {
            const ImVec2 region = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            actions.visible = true;
            actions.region_min = Vector2(region.x, region.y);
            actions.region_max = Vector2(region.x + size.x, region.y + size.y);
            if (!workspace.has_project())
            {
                show_engine_content = true;
            }
            if (!show_engine_content && (folder == "/Engine" || folder.compare(0, 8, "/Engine/") == 0))
            {
                folder = "/Project";
            }
            const auto folder_exists = [&workspace](const std::string& candidate)
            {
                return std::any_of(workspace.catalog().directories.begin(), workspace.catalog().directories.end(),
                                   [&candidate](const VirtualPath& directory)
                                   {
                                       return directory.utf8() == candidate;
                                   });
            };
            // Refresh may remove the current directory; return to its nearest surviving parent.
            while (!folder_exists(folder) && !parent_folder(folder).empty())
            {
                folder = parent_folder(folder);
            }
            if (!folder_exists(folder))
            {
                folder = workspace.has_project() ? "/Project" : "/Engine";
            }
            const auto writable_folder = [&workspace, &folder]()
            {
                return workspace.has_project() && (folder == "/Project" || folder.compare(0, 9, "/Project/") == 0);
            };
            const auto& style = ImGui::GetStyle();
            const float frame = ImGui::GetFrameHeight();
            const float spacing = style.ItemSpacing.x;
            const float toolbar_start = ImGui::GetCursorPosX();
            const float toolbar_available = ImGui::GetContentRegionAvail().x;
            const auto button_width = [&style](const std::string& label)
            {
                return ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f;
            };
            const float add_width = button_width("+ Add");
            const float import_width = button_width("Import");
            const float separator_width = ImGui::CalcTextSize(">").x;
            std::vector<std::string> breadcrumbs;
            const std::string current_path = folder;
            std::size_t segment = 1;
            float path_width = 0.0f;
            while (segment < current_path.size())
            {
                const auto slash = current_path.find('/', segment);
                const std::string path = current_path.substr(0, slash);
                if (!breadcrumbs.empty())
                {
                    path_width += separator_width + spacing * 2.0f;
                }
                breadcrumbs.push_back(path);
                path_width += button_width(folder_label(path));
                if (slash == std::string::npos)
                {
                    break;
                }
                segment = slash + 1;
            }
            // Keep the row left aligned and reserve the remaining width for its path.
            const float controls_width = add_width + import_width + frame * 2.0f + 1.0f + spacing * 5.0f;
            const float available_path_width = std::max(1.0f, toolbar_available - controls_width);
            const bool compact_path = path_width > available_path_width;
            path_width = std::min(path_width, available_path_width);
            ImGui::BeginGroup();
            ImGui::BeginDisabled(!writable_folder());
            if (ImGui::Button("+ Add", ImVec2(add_width, frame)))
            {
                ImGui::OpenPopup("Add Content");
            }
            ImGui::EndDisabled();
            if (ImGui::BeginPopup("Add Content"))
            {
                draw_create_menu(actions, writable_folder(), folder);
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!writable_folder());
            if (ImGui::Button("Import", ImVec2(import_width, frame)))
            {
                actions.import_requested = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip(writable_folder() ? "Import files into the current Content folder"
                                                    : "Select a writable Content folder to import assets");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (property_action_button("Refresh", PropertyAction::Reset,
                                       "Refresh the asset catalog without reimporting source files"))
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
            ImGui::SameLine();
            toolbar_divider(frame);
            ImGui::SameLine();
            // Only action buttons have a resting fill; navigation remains light and clickable.
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_FrameBgHovered]);
            ImGui::BeginDisabled(parent_folder(folder).empty());
            if (ImGui::ArrowButton("Parent Folder", ImGuiDir_Up))
            {
                folder = parent_folder(folder);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Open parent folder");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (compact_path)
            {
                // Narrow panes use one clipped path control; its menu retains every ancestor.
                const std::string label = folder_label(current_path) + "###Compact Path";
                if (ImGui::Button(label.c_str(), ImVec2(path_width, frame)))
                {
                    ImGui::OpenPopup("Folder Path");
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", current_path.c_str());
                }
                if (ImGui::BeginPopup("Folder Path"))
                {
                    for (const auto& path : breadcrumbs)
                    {
                        if (ImGui::MenuItem(path.c_str(), nullptr, path == current_path))
                        {
                            folder = path;
                        }
                    }
                    ImGui::EndPopup();
                }
            }
            else
            {
                for (std::size_t index = 0; index < breadcrumbs.size(); ++index)
                {
                    if (index != 0)
                    {
                        ImGui::SameLine();
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled(">");
                        ImGui::SameLine();
                    }
                    const auto& path = breadcrumbs[index];
                    const std::string label = folder_label(path);
                    ImGui::PushID(path.c_str());
                    if (ImGui::Button(label.c_str(), ImVec2(button_width(label), frame)))
                    {
                        folder = path;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", path.c_str());
                    }
                    ImGui::PopID();
                }
            }
            ImGui::PopStyleColor(2);
            ImGui::EndGroup();
            if (!workspace.error().empty())
            {
                ImGui::TextWrapped("Asset scan: %s", workspace.error().c_str());
            }
            if (!delete_error_.empty())
            {
                ImGui::TextWrapped("Asset operation: %s", delete_error_.c_str());
            }
            ImGui::Separator();
            std::size_t item_count = 0;
            const float footer_height = ImGui::GetFrameHeightWithSpacing();
            if (ImGui::BeginTable(
                    "Content Browser Layout", 2,
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                    ImVec2(0, std::max(ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().y - footer_height))))
            {
                // Keep the same columns when Sources is hidden so saved widths remain meaningful.
                ImGui::TableSetupColumn(
                    "Sources", ImGuiTableColumnFlags_WidthFixed | (show_sources_ ? 0 : ImGuiTableColumnFlags_Disabled),
                    ImGui::GetFontSize() * 12);
                ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                if (show_sources_)
                {
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Sources");
                    ImGui::Separator();
                    ImGui::BeginChild("Folder Tree");
                    const bool reveal = revealed_folder_ != folder;
                    if (workspace.has_project())
                    {
                        draw_folder_tree(workspace.catalog(), "/Project", folder, reveal, actions);
                    }
                    if (show_engine_content)
                    {
                        draw_folder_tree(workspace.catalog(), "/Engine", folder, reveal, actions);
                    }
                    revealed_folder_ = folder;
                    ImGui::EndChild();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##SearchAssets", "Search assets and folders...", search_.data(),
                                         search_.size());
                ImGui::BeginChild("Asset Tiles");
                std::vector<BrowserItem> items;
                for (const auto& directory : workspace.catalog().directories)
                {
                    if (parent_folder(directory.utf8()) == folder &&
                        matches_search(directory.utf8().substr(directory.utf8().find_last_of('/') + 1), search_.data()))
                    {
                        items.push_back({directory.utf8(), nullptr});
                    }
                }
                for (const auto& asset : workspace.catalog().entries)
                {
                    if (parent_folder(asset.path.utf8()) == folder &&
                        matches_search(item_name({asset.path.utf8(), &asset}), search_.data()))
                    {
                        items.push_back({asset.path.utf8(), &asset});
                    }
                }
                item_count = items.size();
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float line_height = ImGui::GetTextLineHeight();
                // Reserve two name lines and the type even in a shallow docked browser.
                const float caption_height = line_height * 3.0f + spacing * 2.0f;
                const float tile_size =
                    std::min(requested_tile_size,
                             std::max(line_height * 2.0f, ImGui::GetContentRegionAvail().y - caption_height));
                const float row_height = tile_size + line_height * 3.0f + spacing * 2.0f;
                // Shallow panels shrink the image, while captions retain a readable card width.
                const float card_width = requested_tile_size;
                const int columns =
                    std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (card_width + spacing)));
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
                            const ImVec2 position(origin.x + column * (card_width + spacing),
                                                  origin.y + row * row_height);
                            const ImVec2 end(position.x + card_width, position.y + row_height - spacing);
                            ImGui::SetCursorScreenPos(position);
                            ImGui::PushID(item.path.c_str());
                            const bool selected = item.asset && selection.asset_id() == item.asset->file.asset_id;
                            if (ImGui::InvisibleButton("Tile", ImVec2(card_width, row_height - spacing)))
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
                                (is_material || item.asset->file.root_type == "toy3d.StaticMeshAssetData" ||
                                 item.asset->file.root_type == "toy3d.SkeletalMeshAssetData" ||
                                 item.asset->file.root_type == "toy3d.AnimationSequenceAssetData") &&
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
                            const ImVec2 thumbnail_position(position.x + (card_width - tile_size) * 0.5f, position.y);
                            if (item.asset)
                            {
                                thumbnail = thumbnails.request(*item.asset);
                            }
                            if (item.asset)
                            {
                                paint_asset_thumbnail(draw, thumbnail_position, tile_size, thumbnail,
                                                      item.asset->file.root_type);
                            }
                            else
                            {
                                draw_asset_placeholder(draw, thumbnail_position, tile_size, true, "");
                            }
                            const std::string name = item_name(item);
                            draw.PushClipRect(position, ImVec2(end.x, end.y - line_height - 6), true);
                            draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                                         ImVec2(position.x + 4, position.y + tile_size + 3), IM_COL32_WHITE,
                                         name.c_str(), nullptr, card_width - 8);
                            draw.PopClipRect();
                            const char* type_label = item.asset ? asset_label(item.asset->file.root_type) : "Folder";
                            const float type_y = end.y - line_height - 3;
                            draw.PushClipRect(ImVec2(position.x, type_y), end, true);
                            draw.AddText(ImVec2(position.x + 4, type_y), IM_COL32(158, 158, 163, 255), type_label);
                            draw.PopClipRect();
                            if (hovered)
                            {
                                ImGui::BeginTooltip();
                                ImGui::TextUnformatted(name.c_str());
                                if (item.asset)
                                {
                                    ImGui::TextDisabled("%s", asset_label(item.asset->file.root_type));
                                    ImGui::TextUnformatted(item.path.c_str());
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
                                const bool is_mesh = item.asset->file.root_type == "toy3d.StaticMeshAssetData" ||
                                                     item.asset->file.root_type == "toy3d.SkeletalMeshAssetData" ||
                                                     item.asset->file.root_type == "toy3d.AnimationSequenceAssetData";
                                const bool writable = item.path.compare(0, 9, "/Project/") == 0;
                                const bool skeletal = item.asset->file.root_type == "toy3d.SkeletalMeshAssetData" ||
                                                      item.asset->file.root_type == "toy3d.AnimationSequenceAssetData";
                                if (skeletal &&
                                    ImGui::MenuItem("Reimport...", nullptr, false, writable && import_enabled))
                                {
                                    actions.skeletal_reimport = item.asset->file.asset_id;
                                }
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
                    ImGui::TextDisabled(search_[0] == '\0' ? "This folder is empty. Add or import content above."
                                                           : "No assets or folders match the search.");
                }
                if (ImGui::BeginPopupContextWindow("Content Actions",
                                                   ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
                {
                    draw_create_menu(actions, writable_folder(), folder);
                    if (!writable_folder())
                    {
                        ImGui::TextDisabled("Engine content is read only");
                    }
                    ImGui::EndPopup();
                }
                ImGui::EndChild();
                ImGui::EndTable();
            }
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%u items", static_cast<unsigned>(item_count));
            if (search_[0] != '\0')
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(filtered)");
            }
            if (!writable_folder())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("| Read only");
            }
            ImGui::EndGroup();
            const float footer_label_width = ImGui::GetItemRectSize().x;
            const float footer_width = ImGui::GetWindowContentRegionMax().x - toolbar_start;
            const float view_width = button_width("View Options");
            const float slider_width = ImGui::GetFontSize() * 8.0f;
            const bool show_slider = footer_width > footer_label_width + slider_width + view_width + spacing * 2.0f;
            const bool compact_view = footer_width < footer_label_width + view_width + spacing;
            const float settings_width = compact_view ? frame : view_width;
            const float footer_controls_width = settings_width + (show_slider ? slider_width + spacing : 0.0f);
            ImGui::SameLine(std::max(toolbar_start + footer_label_width + spacing,
                                     ImGui::GetWindowContentRegionMax().x - footer_controls_width));
            if (show_slider)
            {
                ImGui::SetNextItemWidth(slider_width);
                ImGui::SliderFloat("##ThumbnailSize", &thumbnail_scale_, 0.6f, 1.6f, "Size", ImGuiSliderFlags_NoInput);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Thumbnail size");
                }
                ImGui::SameLine();
            }
            if (ImGui::Button(compact_view ? "...###View Options" : "View Options", ImVec2(settings_width, frame)))
            {
                ImGui::OpenPopup("Browser View Options");
            }
            if (ImGui::BeginPopup("Browser View Options"))
            {
                ImGui::Checkbox("Show Sources", &show_sources_);
                ImGui::BeginDisabled(!workspace.has_project());
                if (ImGui::Checkbox("Show Engine Content", &show_engine_content) && !show_engine_content &&
                    (folder == "/Engine" || folder.compare(0, 8, "/Engine/") == 0))
                {
                    folder = "/Project";
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
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
                        // The deleted identity and its thumbnail entry disappear together.
                        thumbnails.invalidate(pending_delete_);
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
