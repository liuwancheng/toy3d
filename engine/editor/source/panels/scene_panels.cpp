#include "panels/scene_panels.h"

#include "imgui.h"

#include "commands/editor_command_history.h"
#include "gamescene/actor/actor.h"
#include "gamescene/component/light_component.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "selection/editor_selection.h"
#include "workspace/editor_workspace.h"

#include <cstdint>
#include <algorithm>
#include <string>

namespace toy3d
{
    namespace
    {
        std::string parent_folder(const std::string& path)
        {
            const std::size_t separator = path.find_last_of('/');
            return separator == std::string::npos ? std::string() : path.substr(0, separator);
        }

        void draw_transform_field(const char* label, Vector3 Transform::* field, World& world,
                                  Actor& actor, EditorCommandHistory& history)
        {
            SceneComponent* const root = actor.root_component();
            Transform edited = root->local_transform();
            Vector3& value = edited.*field;
            const float speed = field == &Transform::scale ? 0.01f : 0.05f;
            const bool changed = ImGui::DragFloat3(label, value.data(), speed);
            if (ImGui::IsItemActivated() && !history.active())
                history.begin(world, actor.actor_id(), root->local_transform(), EditorTransformSource::Details);
            if (changed && !root->set_local_transform(edited))
                TOY_LOG_ERROR("Details rejected the {} Transform for Actor {}.", label, actor.actor_id());
            if (ImGui::IsItemDeactivated())
                history.finish(world, EditorTransformSource::Details);
        }
    } // namespace

    bool draw_outliner(World& world, EditorSelection& selection, EditorCommandHistory& history, const ActorFactory& factory)
    {
        bool changed = false;
        if (ImGui::Begin("Outliner"))
        {
            selection.resolve_actor(world);
            for (const std::uint32_t actor_id : world.actor_ids())
            {
                const std::string label = std::string(factory.label(actor_id)) + " " + std::to_string(actor_id);
                ImGui::PushID(static_cast<int>(actor_id));
                if (ImGui::Selectable(label.c_str(), selection.actor_id() == actor_id))
                {
                    history.finish(world, EditorTransformSource::Details);
                    selection.select_actor(world, actor_id);
                    changed = true;
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
        return changed;
    }

    void draw_details(World& world, EditorSelection& selection, EditorCommandHistory& history,
                      const EditorWorkspace& workspace)
    {
        if (ImGui::Begin("Details"))
        {
            if (selection.focus() == EditorSelectionFocus::Asset)
            {
                const AssetLocation* const asset = selection.resolve_asset(workspace.catalog().index);
                if (asset != nullptr)
                {
                    ImGui::TextUnformatted(asset->path.utf8().c_str());
                    ImGui::Separator();
                    ImGui::Text("Asset ID: %s", asset->index.asset_id.hex().c_str());
                    ImGui::Text("Type: %s", asset->index.root_type.c_str());
                    ImGui::Text("Schema version: %u", asset->index.schema_version);
                    ImGui::Text("Dependencies: %u", static_cast<unsigned>(asset->index.dependencies.size()));
                    ImGui::TextDisabled("Typed asset editing is not connected yet.");
                }
                else
                    ImGui::TextUnformatted("No Asset selected");
                ImGui::End();
                return;
            }
            Actor* const actor = selection.resolve_actor(world);
            if (actor == nullptr)
                ImGui::TextUnformatted("No Actor selected");
            else
            {
                ImGui::Text("Actor ID: %u", actor->actor_id());
                if (actor->root_component() != nullptr)
                {
                    draw_transform_field("Location", &Transform::translation, world, *actor, history);
                    draw_transform_field("Scale", &Transform::scale, world, *actor, history);
                    ImGui::TextDisabled("Rotation: use the viewport gizmo");
                    auto* light = dynamic_cast<LightComponent*>(actor->root_component());
                    if (light)
                    {
                        ImGui::Separator();
                        EditorActorState edited = capture_actor_state(*actor);
                        bool changed = ImGui::Checkbox("Enabled", &edited.light_enabled);
                        if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                        if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Light enabled edit failed.");
                        if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        edited = capture_actor_state(*actor);
                        changed = ImGui::ColorEdit3("Light Color (linear)", edited.light_color.data());
                        if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                        if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Light color edit failed.");
                        if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        edited = capture_actor_state(*actor);
                        changed = ImGui::DragFloat("Intensity", &edited.light_intensity, 0.05f, 0.0f, 10000.0f);
                        if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                        if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Light intensity edit failed.");
                        if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        if (dynamic_cast<LocalLightComponent*>(light))
                        {
                            edited = capture_actor_state(*actor);
                            changed = ImGui::DragFloat("Range", &edited.light_range, 0.1f, 0.01f, 10000.0f);
                            if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                            if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Light range edit failed.");
                            if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        }
                    }

                }
            }
        }
        ImGui::End();
    }

    void draw_content_browser(EditorWorkspace& workspace, EditorSelection& selection, std::string& folder,
                              bool& show_engine_content)
    {
        if (ImGui::Begin("Content Browser"))
        {
            if (ImGui::Button("Refresh") && !workspace.refresh())
                TOY_LOG_ERROR("Content Browser refresh failed: {}", workspace.error());
            ImGui::SameLine();
            if (ImGui::Checkbox("Show Engine Content", &show_engine_content) && !show_engine_content &&
                (folder == "/Engine" || folder.compare(0, 8, "/Engine/") == 0))
                folder = "/Project";
            ImGui::SameLine();
            ImGui::TextUnformatted(folder.c_str());
            if (folder == "/Engine" || folder.compare(0, 8, "/Engine/") == 0)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(read only)");
            }
            if (!workspace.error().empty())
                ImGui::TextWrapped("Asset scan: %s", workspace.error().c_str());
            ImGui::Separator();

            if (ImGui::BeginTable("Content Browser Columns", 2, ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                for (const VirtualPath& directory : workspace.catalog().directories)
                {
                    const std::string& path = directory.utf8();
                    if (!show_engine_content && (path == "/Engine" || path.compare(0, 8, "/Engine/") == 0))
                        continue;
                    const std::size_t depth = static_cast<std::size_t>(std::count(path.begin(), path.end(), '/'));
                    ImGui::Indent(static_cast<float>(depth > 0 ? depth - 1 : 0) * 12.0f);
                    const std::size_t separator = path.find_last_of('/');
                    const std::string name = path.substr(separator + 1);
                    ImGui::PushID(path.c_str());
                    if (ImGui::Selectable(name.c_str(), folder == path)) folder = path;
                    ImGui::PopID();
                    ImGui::Unindent(static_cast<float>(depth > 0 ? depth - 1 : 0) * 12.0f);
                }
                ImGui::TableSetColumnIndex(1);
                std::size_t visible_assets = 0;
                for (const AssetCatalogEntry& asset : workspace.catalog().entries)
                {
                    if (parent_folder(asset.path.utf8()) != folder) continue;
                    ++visible_assets;
                    const std::string& path = asset.path.utf8();
                    const std::string name = path.substr(path.find_last_of('/') + 1);
                    ImGui::PushID(path.c_str());
                    if (ImGui::Selectable(name.c_str(), selection.asset_id() == asset.file.asset_id))
                        selection.select_asset(asset.file.asset_id);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", asset.file.root_type.c_str());
                    ImGui::PopID();
                }
                if (visible_assets == 0)
                    ImGui::TextDisabled("No .asset files in this folder");
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
} // namespace toy3d
