#include "scene/components/component_details.h"

#include <cstring>
#include "imgui.h"
#include "scene/editor_command_history.h"
#include "gamescene/actor/actor.h"
#include "gamescene/component/static_mesh_component.h"
#include "scene/material_assignments.h"
#include "scene/editor_selection.h"
#include "workspace/editor_workspace.h"
#include "logging/logger.h"

namespace toy3d
{
    namespace
    {
        void draw_material_slots(ComponentDetailsContext& context)
        {
            auto& world = context.world;
            auto& actor = context.actor;
            auto& selection = context.selection;
            auto& history = context.history;
            const auto& workspace = context.workspace;
            auto& materials = context.materials;
            auto& error = context.error;
            auto* component = dynamic_cast<StaticMeshComponent*>(&context.component);
            if (!component || !component->static_mesh())
            {
                return;
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Materials");
            const auto& names = component->static_mesh()->material_slot_names();
            for (const auto& name : names)
            {
                ImGui::PushID(name.c_str());
                ImGui::TextUnformatted(name.c_str());
                const AssetRef current = materials.reference(world, actor.actor_id(), component->component_id(), name);
                const AssetLocation* location =
                    current.asset_id.valid() ? workspace.catalog().index.find(current.asset_id) : nullptr;
                const char* label = location                   ? location->path.utf8().c_str()
                                    : current.asset_id.valid() ? "Missing Material"
                                                               : "Mesh Default";
                auto assign = [&](const AssetId& id)
                {
                    AssetRef reference;
                    if (id.valid())
                    {
                        const auto* asset = workspace.catalog().index.find(id);
                        if (!asset || (asset->index.root_type != "toy3d.MaterialAssetData" &&
                                       asset->index.root_type != "toy3d.MaterialInstanceAssetData"))
                        {
                            error =
                                "The selected Material asset is missing or has changed type. Refresh Content Browser.";
                        }
                        else
                        {
                            reference.asset_id = id;
                            reference.expected_type = asset->index.root_type;
                        }
                        if (!reference.asset_id.valid())
                        {
                            TOY_LOG_ERROR("Material assignment [Actor {} Component {} slot '{}' Asset {}]: {}",
                                          actor.actor_id(), component->component_id(), name, id.hex(), error);
                            return;
                        }
                    }
                    if (!history.assign_material(world, actor.actor_id(), component->component_id(), name, reference,
                                                 error))
                    {
                        materials.offer_compile_assignment(world, actor.actor_id(),
                                                           {component->component_id(), name, reference});
                        TOY_LOG_ERROR("Material assignment [Actor {} Component {} slot '{}' Asset {}]: {}",
                                      actor.actor_id(), component->component_id(), name, id.hex(), error);
                    }
                    else
                    {
                        selection.select_actor(world, actor.actor_id());
                    }
                };
                ImGui::BeginDisabled(history.active());
                ImGui::SetNextItemWidth(-80.0f);
                if (ImGui::BeginCombo("##Material", label))
                {
                    if (ImGui::Selectable("Mesh Default", !current.asset_id.valid()))
                    {
                        assign({});
                    }
                    for (const auto& asset : workspace.catalog().entries)
                    {
                        if (asset.file.root_type == "toy3d.MaterialAssetData" ||
                            asset.file.root_type == "toy3d.MaterialInstanceAssetData")
                        {
                            ImGui::PushID(asset.file.asset_id.hex().c_str());
                            if (ImGui::Selectable(asset.path.utf8().c_str(), current.asset_id == asset.file.asset_id))
                            {
                                assign(asset.file.asset_id);
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(MATERIAL_ASSET_DRAG_PAYLOAD);
                    if (payload && payload->IsDelivery())
                    {
                        AssetId id;
                        if (payload->DataSize == sizeof(AssetId))
                        {
                            std::memcpy(&id, payload->Data, sizeof(id));
                        }
                        if (id.valid())
                        {
                            assign(id);
                        }
                        else
                        {
                            error = "The dragged material has an invalid asset identity.";
                            TOY_LOG_ERROR("Material assignment [Actor {} Component {} slot '{}']: {}", actor.actor_id(),
                                          component->component_id(), name, error);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!current.asset_id.valid());
                if (ImGui::Button("Reset"))
                {
                    assign({});
                }
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::TextDisabled("%s", current.asset_id.valid() ? "Actor override" : "Mesh default material");
                ImGui::PopID();
            }
            if (!error.empty())
            {
                ImGui::TextWrapped("Material assignment failed: %s", error.c_str());
                if (materials.can_compile_assignment() && ImGui::Button("Compile and Assign"))
                {
                    materials.compile_assignment(error);
                }
                if (ImGui::Button("Dismiss Material Error"))
                {
                    error.clear();
                }
            }
        }

    } // namespace

    void draw_mesh_details(ComponentDetailsContext& context)
    {
        SceneComponentData data;
        if (!capture_component_edit(context, data))
        {
            return;
        }
        // The schema has a fixed typed payload; get_if keeps the supported branch explicit.
        auto* mesh = std::get_if<SceneMeshData>(&data.properties);
        if (!mesh)
        {
            return;
        }
        ImGui::BeginDisabled(ImGui::GetDragDropPayload() != nullptr);
        bool changed = ImGui::Checkbox("Visible", &mesh->settings.visible);
        finish_component_edit(context, data, changed);
        changed = ImGui::Checkbox("Cast Shadows", &mesh->settings.cast_shadows);
        finish_component_edit(context, data, changed);
        changed = ImGui::Checkbox("Receive Shadows", &mesh->settings.receives_shadows);
        finish_component_edit(context, data, changed);
        ImGui::EndDisabled();
        draw_material_slots(context);
    }
} // namespace toy3d
