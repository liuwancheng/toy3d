#include "scene/components/component_details.h"

#include "imgui.h"
#include "assets/asset_resource_picker.h"
#include "gamescene/component/skeletal_mesh_component.h"
#include "gamescene/component/static_mesh_component.h"
#include "scene/editor_command_history.h"
#include "scene/editor_selection.h"
#include "scene/material_assignments.h"
#include "scene/mesh_asset_bindings.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        AssetId resource_id(const std::vector<SceneResourceBinding>& resources, const std::string& role)
        {
            for (const auto& resource : resources)
            {
                if (resource.role == role)
                {
                    return resource.reference.asset_id;
                }
            }
            return {};
        }
        void draw_mesh_binding(ComponentDetailsContext& context)
        {
            if (!context.resource_picker || !context.mesh_bindings)
            {
                return;
            }
            auto& factory = context.history.actor_factory();
            SceneMeshData static_data;
            SceneSkeletalMeshData skeletal_data;
            const bool is_static = dynamic_cast<StaticMeshComponent*>(&context.component) != nullptr;
            const bool known = is_static ? factory.mesh_source(context.component, static_data)
                                         : factory.mesh_source(context.component, skeletal_data);
            if (!known)
            {
                ImGui::TextWrapped("Mesh resource has no author identity.");
                return;
            }
            const auto& resources = is_static ? static_data.resources : skeletal_data.resources;
            AssetResourceSelection current{resource_id(resources, "mesh"), is_static ? static_data.builtin_mesh : ""};
            AssetResourceSelection selected;
            ImGui::BeginDisabled(context.history.active() || context.mesh_bindings->busy());
            if (context.resource_picker->draw(is_static ? "Static Mesh" : "Skeletal Mesh", context.workspace, current,
                                              {is_static ? "toy3d.StaticMeshAssetData" : "toy3d.SkeletalMeshAssetData"},
                                              selected, context.error, {}, is_static))
            {
                if (!selected.builtin.empty())
                {
                    context.mesh_bindings->set_builtin(context.world, context.actor.actor_id(),
                                                       context.component.component_id(), selected.builtin);
                }
                else
                {
                    context.mesh_bindings->request(context.world, context.actor.actor_id(),
                                                   context.component.component_id(), "mesh", selected.asset);
                }
            }
            if (!is_static)
            {
                const auto& mesh = static_cast<SkeletalMeshComponent&>(context.component).skeletal_mesh();
                ImGui::BeginDisabled(!mesh);
                current = {resource_id(resources, "animation"), {}};
                const auto filter = [&](const AssetCatalogEntry& entry)
                {
                    return mesh && animation_asset_matches_layout(context.workspace.types(), context.workspace.files(),
                                                                  entry, *mesh->bone_layout());
                };
                if (context.resource_picker->draw("Animation", context.workspace, current,
                                                  {"toy3d.AnimationSequenceAssetData"}, selected, context.error,
                                                  filter))
                {
                    context.mesh_bindings->request(context.world, context.actor.actor_id(),
                                                   context.component.component_id(), "animation", selected.asset);
                }
                ImGui::EndDisabled();
            }
            ImGui::EndDisabled();
            if (context.mesh_bindings->busy())
            {
                ImGui::TextDisabled("Loading resource...");
            }
            if (!context.mesh_bindings->error().empty())
            {
                ImGui::TextWrapped("%s", context.mesh_bindings->error().c_str());
            }
        }
        void draw_material_slots(ComponentDetailsContext& context)
        {
            auto* component = dynamic_cast<MeshComponent*>(&context.component);
            if (!component || !context.resource_picker || component->material_slot_names().empty())
            {
                return;
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Materials");
            ImGui::BeginDisabled(context.history.active() || (context.mesh_bindings && context.mesh_bindings->busy()));
            for (const auto& name : component->material_slot_names())
            {
                ImGui::PushID(name.c_str());
                const auto current = context.materials.reference(context.world, context.actor.actor_id(),
                                                                 component->component_id(), name);
                AssetResourceSelection selected;
                if (context.resource_picker->draw(name.c_str(), context.workspace, {current.asset_id, {}},
                                                  {"toy3d.MaterialAssetData", "toy3d.MaterialInstanceAssetData"},
                                                  selected, context.error))
                {
                    AssetRef reference;
                    const auto* location =
                        selected.asset.valid() ? context.workspace.catalog().index.find(selected.asset) : nullptr;
                    if (location)
                    {
                        reference = {selected.asset, {}, location->index.root_type, AssetRefStrength::Strong};
                    }
                    if (!context.history.assign_material(context.world, context.actor.actor_id(),
                                                         component->component_id(), name, reference, context.error))
                    {
                        context.materials.offer_compile_assignment(context.world, context.actor.actor_id(),
                                                                   {component->component_id(), name, reference});
                    }
                    else
                    {
                        context.selection.select_actor(context.world, context.actor.actor_id());
                    }
                }
                ImGui::TextDisabled("%s", current.asset_id.valid() ? "Actor override" : "Mesh default material");
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            if (!context.error.empty())
            {
                ImGui::TextWrapped("%s", context.error.c_str());
                if (context.materials.can_compile_assignment() && ImGui::Button("Compile and Assign"))
                {
                    context.materials.compile_assignment(context.error);
                }
                if (ImGui::Button("Dismiss Error"))
                {
                    context.error.clear();
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
        // C++17 get_if keeps the two independent author schemas explicit.
        auto* static_data = std::get_if<SceneMeshData>(&data.properties);
        auto* skeletal_data = std::get_if<SceneSkeletalMeshData>(&data.properties);
        if (!static_data && !skeletal_data)
        {
            return;
        }
        draw_mesh_binding(context);
        auto& settings = static_data ? static_data->settings : skeletal_data->settings;
        ImGui::BeginDisabled(ImGui::GetDragDropPayload() != nullptr ||
                             (context.mesh_bindings && context.mesh_bindings->busy()));
        bool changed = ImGui::Checkbox("Visible", &settings.visible);
        finish_component_edit(context, data, changed);
        changed = ImGui::Checkbox("Cast Shadows", &settings.cast_shadows);
        finish_component_edit(context, data, changed);
        changed = ImGui::Checkbox("Receive Shadows", &settings.receives_shadows);
        finish_component_edit(context, data, changed);
        if (skeletal_data)
        {
            changed = ImGui::Checkbox("Loop", &skeletal_data->playback.loop);
            finish_component_edit(context, data, changed);
            changed = ImGui::Checkbox("Autoplay", &skeletal_data->playback.autoplay);
            finish_component_edit(context, data, changed);
            changed = ImGui::InputDouble("Playback Rate", &skeletal_data->playback.rate, 0.1, 1.0, "%.2f");
            finish_component_edit(context, data, changed);
            changed = ImGui::Checkbox("Lock Root", &skeletal_data->lock_root);
            finish_component_edit(context, data, changed);
        }
        ImGui::EndDisabled();
        draw_material_slots(context);
    }
} // namespace toy3d
