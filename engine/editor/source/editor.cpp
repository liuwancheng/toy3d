#include "editor.h"

#include <exception>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "imgui.h"
#include "imgui_internal.h"

#include "gamescene/actor/actor.h"
#include "gamescene/component/static_mesh_component.h"
#include "asset_descriptor_path.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "math/quaternion.h"
#include "panels/place_actors_panel.h"
#include "panels/scene_panels.h"
#include "panels/content_browser_panel.h"
#include "rendercore/frame_synchronization.h"
#include "workspace/editor_workspace.h"
#include "placement/asset_placement.h"
#include "config/command_line_parser.h"

namespace toy3d
{
    namespace
    {
        const char* scene_kind(PlacementItemId item)
        {
            switch (item)
            {
            case PlacementItemId::EmptyActor: return "EmptyActor";
            case PlacementItemId::Cube: return "Cube";
            case PlacementItemId::Plane: return "Plane";
            case PlacementItemId::StaticMesh: return "StaticMesh";
            case PlacementItemId::DirectionalLight: return "DirectionalLight";
            case PlacementItemId::PointLight: return "PointLight";
            case PlacementItemId::Camera: return "Camera";
            }
            return "";
        }

        bool placement_kind(const std::string& kind, PlacementItemId& item)
        {
            for (const PlacementItem& candidate : placement_catalog())
                if (kind == scene_kind(candidate.id)) { item = candidate.id; return true; }
            if (kind == "StaticMesh") { item = PlacementItemId::StaticMesh; return true; }
            return false;
        }
    }

    bool EditorApplication::capture_scene(SceneAssetData& data, std::string& error)
    {
        data.actors.clear();
        const std::vector<std::uint32_t> ids = world().actor_ids();
        for (const std::uint32_t id : ids)
        {
            Actor* actor = world().find_actor_by_id(id);
            if (!actor || !actor->root_component() || actor->component_count() != 1u)
            { error = "Scene contains an Actor with unsupported components."; return false; }
            if (stable_scene_ids_.count(id) == 0u)
            {
                AssetId actor_id;
                AssetId component_id;
                if (!AssetId::try_generate(actor_id) || !AssetId::try_generate(component_id))
                { error = "Could not allocate stable Scene object IDs."; return false; }
                stable_scene_ids_[id] = {actor_id.hex(), component_id.hex()};
            }
        }
        for (const std::uint32_t id : ids)
        {
            Actor* actor = world().find_actor_by_id(id);
            PlacementRequest request;
            if (!actor_factory_.describe(*actor, request))
            { error = "Scene contains an Actor without a supported placement description."; return false; }
            SceneActorData saved;
            saved.id = stable_scene_ids_.at(id).first;
            saved.root_component_id = stable_scene_ids_.at(id).second;
            saved.kind = scene_kind(request.item);
            saved.root_component_type = scene_root_component_type(saved.kind);
            if (saved.kind.empty()) { error = "Scene contains an unsupported Actor kind."; return false; }
            const EditorActorState state = capture_actor_state(*actor);
            saved.transform = state.transform;
            saved.primitive_cast_shadows = state.primitive_cast_shadows;
            saved.primitive_receives_shadows = state.primitive_receives_shadows;
            saved.light_enabled = state.light_enabled;
            saved.light_color = state.light_color;
            saved.light_intensity = state.light_intensity;
            saved.light_range = state.light_range;
            saved.light_priority = state.light_priority;
            saved.shadow_cast_shadows = state.shadow_cast_shadows;
            saved.shadow_distance = state.shadow_distance;
            saved.shadow_distance_fade_fraction = state.shadow_distance_fade_fraction;
            saved.shadow_bias = state.shadow_bias;
            saved.shadow_slope_bias = state.shadow_slope_bias;
            saved.camera_vertical_fov = state.camera_vertical_fov;
            saved.camera_near_clip = state.camera_near_clip;
            saved.camera_far_clip = state.camera_far_clip;
            const SceneComponent* parent = actor->root_component()->parent();
            if (parent)
            {
                const auto found = stable_scene_ids_.find(parent->owner().actor_id());
                if (found == stable_scene_ids_.end() || parent != parent->owner().root_component())
                { error = "Scene contains an unsupported attachment target."; return false; }
                saved.parent_component_id = found->second.second;
            }
            if (request.item == PlacementItemId::StaticMesh)
            {
                if (!request.asset_id.valid())
                { error = "StaticMesh Actor has no source Asset ID."; return false; }
                saved.resources.push_back({"mesh", {request.asset_id, {}, "toy3d.StaticMeshAssetData",
                    AssetRefStrength::Strong}});
            }
            const auto* mesh = dynamic_cast<const StaticMeshComponent*>(actor->root_component());
            const auto assignments = material_assignments_.capture(world(), id);
            if (!assignments.empty() && !mesh)
            { error = "Material assignments target a non-mesh Actor."; return false; }
            if (mesh && mesh->static_mesh())
            {
                const auto& slots = mesh->static_mesh()->material_slot_names();
                for (std::size_t slot = 0; slot < slots.size(); ++slot)
                {
                    const auto found = std::find_if(assignments.begin(), assignments.end(),
                        [&](const MaterialSlotAssignment& assignment)
                        { return assignment.component_id == mesh->component_id() && assignment.slot_name == slots[slot]; });
                    if (mesh->has_material_override(static_cast<std::uint32_t>(slot)) != (found != assignments.end()))
                    { error = "Scene has a material override without an Editor Asset reference."; return false; }
                    if (found != assignments.end()) saved.resources.push_back({"material:" + slots[slot], found->material});
                }
                if (assignments.size() != saved.resources.size() -
                    (request.item == PlacementItemId::StaticMesh ? 1u : 0u))
                { error = "Scene has an unknown material slot assignment."; return false; }
            }
            data.actors.push_back(std::move(saved));
        }
        const AssetStatus valid = validate_scene_asset(data, &workspace_.catalog().index);
        if (!valid.succeeded()) { error = valid.message; return false; }
        error.clear();
        return true;
    }

    bool EditorApplication::replace_scene(const SceneAssetData& data, std::string& error)
    {
        struct PreparedActor { PlacementRequest placement; EditorActorState state; };
        std::vector<PreparedActor> prepared;
        prepared.reserve(data.actors.size());
        for (const SceneActorData& saved : data.actors)
        {
            PreparedActor candidate;
            if (!placement_kind(saved.kind, candidate.placement.item))
            { error = "Unsupported Scene Actor kind: " + saved.kind; return false; }
            candidate.placement.transform = saved.transform;
            candidate.state.transform = saved.transform;
            candidate.state.primitive_cast_shadows = saved.primitive_cast_shadows;
            candidate.state.primitive_receives_shadows = saved.primitive_receives_shadows;
            candidate.state.light_enabled = saved.light_enabled;
            candidate.state.light_color = saved.light_color;
            candidate.state.light_intensity = saved.light_intensity;
            candidate.state.light_range = saved.light_range;
            candidate.state.light_priority = saved.light_priority;
            candidate.state.shadow_cast_shadows = saved.shadow_cast_shadows;
            candidate.state.shadow_distance = saved.shadow_distance;
            candidate.state.shadow_distance_fade_fraction = saved.shadow_distance_fade_fraction;
            candidate.state.shadow_bias = saved.shadow_bias;
            candidate.state.shadow_slope_bias = saved.shadow_slope_bias;
            candidate.state.camera_vertical_fov = saved.camera_vertical_fov;
            candidate.state.camera_near_clip = saved.camera_near_clip;
            candidate.state.camera_far_clip = saved.camera_far_clip;
            for (const SceneResourceBinding& binding : saved.resources)
            {
                if (binding.role != "mesh") continue;
                const AssetLocation* location = workspace_.catalog().index.find(binding.reference.asset_id);
                if (!location) { error = "Scene StaticMesh asset is missing."; return false; }
                const auto geometry = read_static_mesh_asset(workspace_.files(), location->path);
                if (!geometry.succeeded()) { error = geometry.status().message; return false; }
                candidate.placement.asset_id = binding.reference.asset_id;
                candidate.placement.static_mesh = create_static_mesh_from_asset(geometry.value(),
                    actor_factory_.default_material());
                if (!candidate.placement.static_mesh)
                { error = "Could not construct Scene StaticMesh geometry."; return false; }
            }
            prepared.push_back(std::move(candidate));
        }
        const std::vector<std::uint32_t> old_ids = world().actor_ids();
        std::vector<std::uint32_t> new_ids;
        std::map<std::string, SceneComponent*> components;
        std::map<std::uint32_t, std::pair<std::string, std::string>> next_stable_ids;
        auto rollback = [&]()
        {
            for (const std::uint32_t id : new_ids)
            {
                Actor* actor = world().find_actor_by_id(id);
                if (actor && !world().destroy_actor(*actor)) TOY_LOG_ERROR("Scene candidate rollback failed.");
                actor_factory_.forget(id);
                material_assignments_.forget(id);
            }
        };
        for (std::size_t i = 0; i < data.actors.size(); ++i)
        {
            Actor* actor = actor_factory_.create(world(), prepared[i].placement);
            if (!actor)
            { error = "Could not construct Scene Actor."; rollback(); return false; }
            new_ids.push_back(actor->actor_id());
            components[data.actors[i].root_component_id] = actor->root_component();
            next_stable_ids[actor->actor_id()] = {data.actors[i].id, data.actors[i].root_component_id};
            if (!apply_actor_state(*actor, prepared[i].state))
            { error = "Could not restore Scene Actor properties."; rollback(); return false; }
            for (const SceneResourceBinding& binding : data.actors[i].resources)
            {
                if (binding.role == "mesh") continue;
                MaterialSlotAssignment assignment;
                assignment.component_id = actor->root_component()->component_id();
                assignment.slot_name = binding.role.substr(9u);
                assignment.material = binding.reference;
                if (!material_assignments_.assign(world(), actor->actor_id(), assignment, error))
                { rollback(); return false; }
            }
        }
        for (const SceneActorData& saved : data.actors)
        {
            if (saved.parent_component_id.empty()) continue;
            if (!components.at(saved.root_component_id)->attach_to(
                components.at(saved.parent_component_id), AttachmentRule::KeepRelative))
            { error = "Could not restore Scene attachment."; rollback(); return false; }
        }
        for (const std::uint32_t id : old_ids)
        {
            Actor* actor = world().find_actor_by_id(id);
            if (actor && !world().destroy_actor(*actor))
            { error = "Could not remove the previous Scene Actor."; rollback(); return false; }
            actor_factory_.forget(id);
            material_assignments_.forget(id);
        }
        stable_scene_ids_ = std::move(next_stable_ids);
        command_history_.clear();
        selection_.clear_actor();
        scene_viewport_.exit_camera_view();
        scene_viewport_.cancel_pending_hit();
        error.clear();
        return true;
    }

    bool EditorApplication::scene_dirty()
    {
        if (!scene_id_.valid()) return !world().actor_ids().empty();
        SceneAssetData snapshot;
        std::string error;
        if (!capture_scene(snapshot, error)) return true;
        const auto encoded = encode_scene_asset_pair(workspace_.types(), scene_id_, snapshot,
            &workspace_.catalog().index);
        return !encoded.succeeded() || encoded.value().asset != scene_baseline_;
    }

    bool EditorApplication::save_scene(const VirtualPath& path, bool create_new)
    {
        if (asset_descriptor_kind(path) != AssetDescriptorKind::Scene ||
            path.utf8().compare(0u, 9u, "/Project/") != 0)
        { scene_error_ = "Choose a .scene path inside Project assets."; return false; }
        SceneAssetData snapshot;
        if (!capture_scene(snapshot, scene_error_)) return false;
        AssetId id = scene_id_;
        if (create_new || !id.valid())
        {
            if (!AssetId::try_generate(id) || workspace_.catalog().index.find(id))
            { scene_error_ = "Could not allocate a unique Scene Asset ID."; return false; }
        }
        else
        {
            const auto disk = workspace_.files().read_binary(path, scene_baseline_.size() + 1u);
            if (!disk.succeeded() || disk.value() != scene_baseline_)
            { scene_error_ = "Scene file changed on disk. Use Save Scene As or reopen it."; return false; }
        }
        const auto encoded = encode_scene_asset_pair(workspace_.types(), id, snapshot,
            &workspace_.catalog().index);
        if (!encoded.succeeded()) { scene_error_ = encoded.status().message; return false; }
        const AssetStatus published = workspace_.asset_pairs().publish(path, encoded.value(),
            create_new ? FilePublishMode::CreateNew : FilePublishMode::Replace);
        if (!published.succeeded()) { scene_error_ = published.message; return false; }
        scene_id_ = id;
        scene_path_ = path;
        scene_baseline_ = encoded.value().asset;
        if (!workspace_.refresh())
        { scene_error_ = "Scene was saved, but Content Browser refresh failed: " + workspace_.error(); return false; }
        selection_.select_asset(id);
        scene_error_.clear();
        return true;
    }

    bool EditorApplication::open_scene(const AssetId& id)
    {
        const AssetLocation* location = workspace_.catalog().index.find(id);
        if (!location || asset_descriptor_kind(location->path) != AssetDescriptorKind::Scene)
        { scene_error_ = "Scene asset is missing from Content Browser."; return false; }
        SceneAssetData loaded;
        const AssetStatus read = read_scene_asset(workspace_.types(), workspace_.files(),
            location->path, loaded, &workspace_.catalog().index);
        if (!read.succeeded()) { scene_error_ = read.message; return false; }
        const auto bytes = workspace_.files().read_binary(location->path);
        if (!bytes.succeeded()) { scene_error_ = bytes.status().message; return false; }
        if (!replace_scene(loaded, scene_error_)) return false;
        scene_id_ = id;
        scene_path_ = location->path;
        scene_baseline_ = bytes.value();
        selection_.select_asset(id);
        return true;
    }

    void EditorApplication::new_scene()
    {
        command_history_.clear();
        selection_.clear_actor();
        scene_viewport_.exit_camera_view();
        scene_viewport_.cancel_pending_hit();
        for (const std::uint32_t id : world().actor_ids())
        {
            Actor* actor = world().find_actor_by_id(id);
            if (actor && !world().destroy_actor(*actor))
            { scene_error_ = "Could not clear the current Scene."; return; }
            actor_factory_.forget(id);
            material_assignments_.forget(id);
        }
        scene_id_ = {};
        scene_path_ = {};
        scene_baseline_.clear();
        stable_scene_ids_.clear();
        scene_error_.clear();
    }

    void EditorApplication::request_scene_action(SceneAction action, const AssetId& id)
    {
        pending_scene_action_ = action;
        pending_scene_id_ = id;
        if (scene_dirty()) scene_confirm_requested_ = true;
        else
        {
            pending_scene_action_ = SceneAction::None;
            if (action == SceneAction::New) new_scene();
            else if (action == SceneAction::Open) open_scene(id);
            else if (action == SceneAction::Exit) window().close();
        }
    }

    bool EditorApplication::on_close_requested()
    {
        if (!material_editor_.request_exit()) return false;
        if (discard_scene_on_exit_) return true;
        if (!scene_dirty()) return true;
        pending_scene_action_ = SceneAction::Exit;
        scene_confirm_requested_ = true;
        return false;
    }

    void EditorApplication::draw_scene_dialogs()
    {
        if (scene_confirm_requested_)
        {
            ImGui::OpenPopup("Unsaved Scene");
            scene_confirm_requested_ = false;
        }
        if (ImGui::BeginPopupModal("Unsaved Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("The current Scene has unsaved changes.");
            if (ImGui::Button("Save"))
            {
                if (scene_path_.empty())
                { show_scene_save_as_ = true; ImGui::CloseCurrentPopup(); }
                else if (save_scene(scene_path_, false))
                {
                    const SceneAction action = pending_scene_action_;
                    const AssetId id = pending_scene_id_;
                    pending_scene_action_ = SceneAction::None;
                    ImGui::CloseCurrentPopup();
                    request_scene_action(action, id);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
            {
                const SceneAction action = pending_scene_action_;
                const AssetId id = pending_scene_id_;
                pending_scene_action_ = SceneAction::None;
                ImGui::CloseCurrentPopup();
                if (action == SceneAction::New) new_scene();
                else if (action == SceneAction::Open) open_scene(id);
                else if (action == SceneAction::Exit)
                { discard_scene_on_exit_ = true; window().close(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                pending_scene_action_ = SceneAction::None;
                ImGui::CloseCurrentPopup();
            }
            if (!scene_error_.empty()) ImGui::TextWrapped("%s", scene_error_.c_str());
            ImGui::EndPopup();
        }
        if (show_scene_save_as_) ImGui::OpenPopup("Save Scene As");
        if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Folder: %s", asset_folder_.c_str());
            ImGui::InputText("Scene name", scene_name_, sizeof(scene_name_));
            if (ImGui::Button("Save Scene"))
            {
                const std::string name = scene_name_;
                const bool valid_name = !name.empty() && std::all_of(name.begin(), name.end(),
                    [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
                if (!valid_name) scene_error_ = "Use letters, numbers, underscore or dash for the Scene name.";
                else
                {
                    const auto path = VirtualPath::parse(asset_folder_ + "/" + name + ".scene");
                    if (!path.succeeded()) scene_error_ = path.status().message;
                    else if (save_scene(path.value(), true))
                    {
                        show_scene_save_as_ = false;
                        ImGui::CloseCurrentPopup();
                        if (pending_scene_action_ != SceneAction::None)
                        {
                            const SceneAction action = pending_scene_action_;
                            const AssetId id = pending_scene_id_;
                            pending_scene_action_ = SceneAction::None;
                            request_scene_action(action, id);
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                show_scene_save_as_ = false;
                pending_scene_action_ = SceneAction::None;
                ImGui::CloseCurrentPopup();
            }
            if (!scene_error_.empty()) ImGui::TextWrapped("%s", scene_error_.c_str());
            ImGui::EndPopup();
        }
        if (!scene_error_.empty() && !show_scene_save_as_ && pending_scene_action_ == SceneAction::None)
        {
            if (ImGui::Begin("Scene Error"))
            {
                ImGui::TextWrapped("%s", scene_error_.c_str());
                if (ImGui::Button("Dismiss")) scene_error_.clear();
            }
            ImGui::End();
        }
    }
    bool EditorApplication::on_initialize()
    {
        if (ImGui::GetCurrentContext() == nullptr)
            return false;
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.TabRounding = 3.0f;
        ImGui::GetIO().IniFilename = TOY3D_EDITOR_LAYOUT_PATH;
        // Scene-image gestures edit content; dock panels move by their title/tab.
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
        if (!window().enable_file_drop(true)) TOY_LOG_WARN("External asset file drop is unavailable on this platform.");
        if (!actor_factory_.initialize()) return false;
        MaterialTextureValues textures;
        const auto defaults = actor_factory_.default_material()->material();
        for (const auto& resource : defaults->parameter_schema().resources)
        {
            const auto found = defaults->desc().texture_defaults.find(resource.parameter_id);
            if (found != defaults->desc().texture_defaults.end()) textures.named_defaults[resource.default_value] = found->second;
        }
        materials_ = std::make_unique<MaterialLibrary>(workspace_.types(), workspace_.files(),
            [this]() -> const AssetIndex& { return workspace_.catalog().index; },
            [this, defaults](const std::string& name)
            {
                return shader_workflow_ready_ ? shaders_.program(name) :
                    (name == defaults->desc().shader_name ? defaults->desc().shader_program : nullptr);
            }, std::move(textures));
        material_assignments_.initialize(workspace_, *materials_);
        workspace_.material_edit().set_publish([this](const AssetRef& reference) { return materials_->reload(reference); });
        material_editor_.initialize(workspace_, actor_factory_.default_material()->material(),
            PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
        auto& arguments = CommandLineParser::get_instance();
        MaterialShaderPaths shader_paths;
        shader_paths.project_shader = PhysicalPath(arguments.get_option("Editor.ProjectShaderRoot", TOY3D_EDITOR_PROJECT_SHADER_ROOT));
        shader_paths.project_config = PhysicalPath(arguments.get_option("Editor.ShaderConfigRoot", TOY3D_EDITOR_PROJECT_CONFIG_ROOT));
        shader_paths.engine_shader = PhysicalPath(TOY3D_EDITOR_ENGINE_SHADER_ROOT);
        shader_paths.engine_include = PhysicalPath(TOY3D_EDITOR_ENGINE_INCLUDE_ROOT);
        shader_paths.builtin_entries = PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT);
        shader_paths.saved = PhysicalPath(TOY3D_EDITOR_SHADER_SAVED_ROOT);
        shader_paths.compiler = PhysicalPath(TOY3D_EDITOR_SHADER_COMPILER);
        shader_paths.toolchain = PhysicalPath(TOY3D_EDITOR_SHADER_TOOLCHAIN);
        shader_paths.code_executable = PhysicalPath(arguments.get_option("Editor.CodeExecutable", ""));
        std::string shader_error;
        shader_workflow_ready_ = shaders_.initialize(std::move(shader_paths), actor_factory_.default_material()->material(), shader_error);
        if (shader_workflow_ready_)
        {
            material_editor_.set_shader_workflow(shaders_);

        }
        else TOY_LOG_ERROR("Material source workflow unavailable: {}", shader_error);
        PlacementRequest preview;
        preview.item = PlacementItemId::Cube;
        preview.transform.translation = Vector3(0.0f, 0.75f, 3.0f);
        if (!actor_factory_.create(world(), preview)) return false;
        preview.item = PlacementItemId::DirectionalLight;
        preview.transform.translation = Vector3(0.0f, 3.0f, 3.0f);
        if (!try_make_rotation_from_forward_up(Vector3(-0.35f, -0.55f, 0.75f),
                                                Vector3(0, 1, 0), preview.transform.rotation)) return false;
        if (!actor_factory_.create(world(), preview)) return false;
        return true;
    }

    void EditorApplication::on_tick(double)
    {
        thumbnails_.tick();
        texture_preview_.tick();
        if (!shader_workflow_ready_) return;
        shaders_.tick();
        if (!shaders_.candidate_ready()) return;
        auto& session = workspace_.material_edit();
        if (shaders_.origin().valid() && (!session.active() || !(session.id() == shaders_.origin()) ||
            material_editor_.session_revision() != shaders_.origin_revision()))
        { shaders_.reject("The material session changed during compilation. Recompile from the current session."); return; }
        if (session.gesturing()) return;
        std::string error;
        try
        {
            if (!material_assignments_.prepare_shader(shaders_.candidate(), error) ||
                !material_editor_.prepare_shader(shaders_.candidate(), shaders_.candidate_properties(), error))
            {
                material_assignments_.discard_shader(); material_editor_.discard_shader(); shaders_.reject(error); return;
            }
            // GT candidates/target identities and RT pipelines are checked
            // before publishing the source record and the slot references.
            if (!material_assignments_.publish_shader(error, true))
            { material_assignments_.discard_shader(); material_editor_.discard_shader(); shaders_.reject(error); return; }
            if (!shaders_.publish())
            {
                error = shaders_.error(); material_assignments_.discard_shader(); material_editor_.discard_shader(); shaders_.reject(error); return;
            }
            material_assignments_.complete_shader();
            material_editor_.publish_shader();
            TOY_LOG_INFO("{}", shaders_.status());
        }
        catch (const std::exception& exception)
        { material_assignments_.discard_shader(); material_editor_.discard_shader(); shaders_.reject(exception.what()); }
    }

    void EditorApplication::on_shutdown()
    {
        shaders_.shutdown();
        if (!window().enable_file_drop(false)) TOY_LOG_WARN("Could not disable external asset file drop.");
        model_import_.clear();
        material_create_.clear();
        material_editor_.shutdown();
        texture_preview_.shutdown();
        thumbnails_.shutdown();
        scene_viewport_.exit_camera_view();
        command_history_.clear();
        for (const auto actor_id : world().actor_ids())
        {
            Actor* actor = world().find_actor_by_id(actor_id);
            if (actor && !world().destroy_actor(*actor)) TOY_LOG_ERROR("Editor Actor teardown failed.");
        }
        const RenderFenceWaitResult drained = flush_rendering_commands();
        if (!drained.succeeded())
            TOY_LOG_ERROR("Editor preview scene could not drain before material release: {}",
                          drained.framework_status().message);
        command_history_.cancel();
        selection_.clear_actor();
        selection_.clear_asset();
        material_assignments_.shutdown();
        workspace_.material_edit().set_publish({});
        if (materials_) { materials_->shutdown(); materials_.reset(); }
        actor_factory_.release();
    }

    void EditorApplication::on_build_ui()
    {
        scene_viewport_.begin_frame();
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene")) request_scene_action(SceneAction::New);
                if (ImGui::BeginMenu("Open Scene"))
                {
                    for (const AssetCatalogEntry& entry : workspace_.catalog().entries)
                        if (asset_descriptor_kind(entry.path) == AssetDescriptorKind::Scene &&
                            ImGui::MenuItem(entry.path.utf8().c_str()))
                            request_scene_action(SceneAction::Open, entry.file.asset_id);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                {
                    if (scene_path_.empty()) show_scene_save_as_ = true;
                    else save_scene(scene_path_, false);
                }
                if (ImGui::MenuItem("Save Scene As...")) show_scene_save_as_ = true;
                ImGui::Separator();
                if (ImGui::BeginMenu("Create Asset", !model_import_.active() && !texture_import_.active() && !material_create_.active()))
                {
                    if (ImGui::MenuItem("Create Material...")) material_create_.request(MaterialAssetCreationKind::Material, asset_folder_);
                    if (ImGui::MenuItem("Create Material Instance...")) material_create_.request(MaterialAssetCreationKind::MaterialInstance, asset_folder_);
                    ImGui::EndMenu();
                }
#if WITH_MODEL_IMPORT
                if (ImGui::MenuItem("Import Static Mesh...", nullptr, false,
                    !material_create_.active() && !texture_import_.active()))
                {
                    if (!model_import_.request(asset_folder_)) model_error_ = model_import_.error();
                }
#endif
                if (ImGui::MenuItem("Import Texture2D...", nullptr, false,
                    !model_import_.active() && !material_create_.active()))
                {
                    if (!texture_import_.request(asset_folder_)) model_error_ = texture_import_.error();
                }
                if (ImGui::MenuItem("Exit")) request_scene_action(SceneAction::Exit);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) undo_edit();
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) redo_edit();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("Reset Layout")) reset_dock_layout_ = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Scene Viewport")) ImGui::SetWindowFocus("Scene Viewport###Game Viewport");
                if (ImGui::MenuItem("Place Actors")) ImGui::SetWindowFocus("Place Actors");
                if (ImGui::MenuItem("Outliner")) ImGui::SetWindowFocus("Outliner");
                if (ImGui::MenuItem("Details")) ImGui::SetWindowFocus("Details");
                if (ImGui::MenuItem("Content Browser")) ImGui::SetWindowFocus("Content Browser");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About Toy3d Editor")) ImGui::OpenPopup("About Toy3d Editor");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (ImGui::BeginPopupModal("About Toy3d Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Toy3d Editor");
            ImGui::TextUnformatted("Scene and asset workspace");
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        const ImGuiViewport* const viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        constexpr ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                ImGuiWindowFlags_NoNavFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Toy3d Editor Dockspace", nullptr, host_flags);
        ImGui::PopStyleVar(3);
        const ImGuiID dockspace = ImGui::GetID("Toy3d Editor Dockspace Node");
        if (reset_dock_layout_)
        {
            ImGui::DockBuilderRemoveNode(dockspace);
            initial_dock_layout_checked_ = false;
            reset_dock_layout_ = false;
        }
        if (!initial_dock_layout_checked_)
        {
            initial_dock_layout_checked_ = true;
            if (ImGui::DockBuilderGetNode(dockspace) == nullptr ||
                ImGui::FindWindowByName("Place Actors") == nullptr)
            {
                ImGui::DockBuilderRemoveNode(dockspace);
                ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
                ImVec2 dock_size = ImGui::GetContentRegionAvail();
                dock_size.y = dock_size.y > 28.0f ? dock_size.y - 28.0f : 0.0f;
                ImGui::DockBuilderSetNodeSize(dockspace, dock_size);
                ImGuiID content_dock = 0;
                ImGuiID upper_dock = 0;
                ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Down, 0.27f,
                                            &content_dock, &upper_dock);
                ImGuiID right_dock = 0;
                ImGuiID scene_dock = 0;
                ImGui::DockBuilderSplitNode(upper_dock, ImGuiDir_Right, 0.25f,
                                            &right_dock, &scene_dock);
                ImGuiID placement_dock = 0;
                ImGui::DockBuilderSplitNode(scene_dock, ImGuiDir_Left, 0.22f, &placement_dock, &scene_dock);
                ImGui::DockBuilderDockWindow("Place Actors", placement_dock);
                ImGuiID outliner_dock = 0;
                ImGuiID details_dock = 0;
                ImGui::DockBuilderSplitNode(right_dock, ImGuiDir_Up, 0.55f,
                                            &outliner_dock, &details_dock);
                ImGui::DockBuilderDockWindow("Scene Viewport###Game Viewport", scene_dock);
                ImGui::DockBuilderDockWindow("Outliner", outliner_dock);
                ImGui::DockBuilderDockWindow("Details", details_dock);
                ImGui::DockBuilderDockWindow("Texture Preview", details_dock);
                ImGui::DockBuilderDockWindow("Content Browser", content_dock);
                ImGui::DockBuilderFinish(dockspace);
            }
        }
        ImGui::DockSpace(dockspace, ImVec2(0.0f, -28.0f));
        ImGui::Separator();
        ImGui::Text("Assets: %u", static_cast<unsigned>(workspace_.catalog().entries.size()));
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Scene: %s%s", scene_path_.empty() ? "Untitled" : scene_path_.utf8().c_str(),
            scene_dirty() ? " *" : "");
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Source: %s", workspace_.source_root().utf8().c_str());
        if (!workspace_.error().empty())
        {
            ImGui::SameLine();
            ImGui::Text("  |  Asset scan failed: %s", workspace_.error().c_str());
        }
        ImGui::End();

        draw_place_actors_panel();
        if (draw_outliner(world(), selection_, command_history_, actor_factory_, scene_viewport_))
            scene_viewport_.cancel_pending_hit();
        draw_details(world(), selection_, command_history_, workspace_, scene_viewport_, material_assignments_, material_assignment_error_);
        scene_viewport_.draw(world(), selection_, command_history_);
        const ContentBrowserActions browser = draw_content_browser(workspace_, selection_, asset_folder_,
            show_engine_content_, thumbnails_, WITH_MODEL_IMPORT != 0);
        if (browser.assets_refreshed) texture_preview_.invalidate();
        if (browser.texture_open.valid()) texture_preview_.request_open(browser.texture_open, browser.texture_focus);
        texture_preview_.draw();
        if (browser.scene_open.valid()) request_scene_action(SceneAction::Open, browser.scene_open);
        if (browser.material_open.valid() && !model_import_.active() && !texture_import_.active() && !material_create_.active())
            material_editor_.request_open(browser.material_open);
        if (browser.material_creation_requested && !model_import_.active() && !texture_import_.active())
            material_create_.request(browser.material_creation_kind, asset_folder_, browser.material_parent);
        if (browser.texture_import_requested && !material_create_.active() && !model_import_.active() &&
            !texture_import_.request(asset_folder_)) model_error_ = texture_import_.error();
#if WITH_MODEL_IMPORT
        if (browser.import_requested && !material_create_.active() && !texture_import_.active() &&
            !model_import_.request(asset_folder_)) model_error_ = model_import_.error();
#endif
        FileDropEvent dropped;
        while (window().take_file_drop(dropped))
        {
            if (model_import_.active() || texture_import_.active() || material_create_.active() ||
                ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || !browser.accepts_drop(dropped.position)) continue;
            bool image = false;
            bool model = false;
            for (const auto& path : dropped.paths)
            {
                std::string extension = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") image = true;
                else model = true;
            }
            if (image && model) model_error_ = "Drop image and model files separately.";
            else if (image && !texture_import_.request(asset_folder_, dropped.paths)) model_error_ = texture_import_.error();
#if WITH_MODEL_IMPORT
            else if (model && !model_import_.request(asset_folder_, dropped.paths)) model_error_ = model_import_.error();
#else
            else if (model) model_error_ = "Model import is disabled in this build.";
#endif
        }
#if WITH_MODEL_IMPORT
        model_import_.draw(window(), workspace_, selection_, thumbnails_);
#endif
        texture_import_.draw(window(), workspace_, selection_);
        material_create_.draw(workspace_, selection_, asset_folder_, actor_factory_.default_material()->material()->parameter_schema(),
            shader_workflow_ready_ ? &shaders_ : nullptr);
        material_editor_.draw();
        draw_scene_dialogs();
        const auto locate = material_editor_.take_locate_parent();
        if (locate.valid())
        {
            const auto* location = workspace_.catalog().index.find(locate);
            if (location)
            {
                selection_.select_asset(locate);
                asset_folder_ = location->path.utf8().substr(0, location->path.utf8().find_last_of('/'));
                if (asset_folder_.compare(0, 7, "/Engine") == 0) show_engine_content_ = true;
            }
        }
        if (material_editor_.take_exit()) window().close();
        AssetPlacementRequest placed;
        if (scene_viewport_.take_asset_placement(placed))
        {
            const std::uint32_t actor_id = place_static_mesh_asset(workspace_, world(), actor_factory_,
                command_history_, placed, model_error_);
            if (actor_id)
            {
                selection_.select_actor(world(), actor_id);
                scene_viewport_.cancel_pending_hit();
            }
            else TOY_LOG_ERROR("Asset placement failed: {}", model_error_);
        }
        if (!model_error_.empty())
        {
            if (ImGui::Begin("Model Import / Load"))
            {
                ImGui::TextWrapped("%s", model_error_.c_str());
                if (ImGui::Button("Dismiss")) model_error_.clear();
            }
            ImGui::End();
        }

        selection_.resolve_actor(world());
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && !io.WantTextInput &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        {
            if (scene_path_.empty()) show_scene_save_as_ = true;
            else save_scene(scene_path_, false);
        }
        const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
        if (focused) focused = focused->RootWindow;
        const bool actor_panel_focused = focused &&
            (focused == ImGui::FindWindowByName("Scene Viewport###Game Viewport") ||
             focused == ImGui::FindWindowByName("Outliner") || focused == ImGui::FindWindowByName("Details"));
        if (material_editor_.focused()) material_history_target_ = true;
        else if (actor_panel_focused) material_history_target_ = false;
        const bool modal_active = model_import_.active() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        if (!modal_active && actor_panel_focused && selection_.focus() == EditorSelectionFocus::Actor &&
            !io.WantTextInput && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (command_history_.delete_actor(world(), selection_.actor_id()))
            {
                selection_.clear_actor();
                scene_viewport_.cancel_pending_hit();
            }
        }
        if (!modal_active && actor_panel_focused && io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z))
            {
                if (io.KeyShift)
                    command_history_.redo(world());
                else
                    command_history_.undo(world());
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Y))
                command_history_.redo(world());
        }
    }

    void EditorApplication::undo_edit()
    {
        if (ImGui::IsAnyItemActive() || model_import_.active() || material_create_.active() || material_editor_.modal_pending()) return;
        if (material_history_target_) material_editor_.undo();
        else command_history_.undo(world());
    }

    void EditorApplication::redo_edit()
    {
        if (ImGui::IsAnyItemActive() || model_import_.active() || material_create_.active() || material_editor_.modal_pending()) return;
        if (material_history_target_) material_editor_.redo();
        else command_history_.redo(world());
    }

    bool EditorApplication::on_initialize_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks)
    {
        return thumbnails_.initialize(scene, actor_factory_.default_material(), tasks);
    }

    bool EditorApplication::on_hit_proxy_request(HitProxyRequest& request)
    {
        return scene_viewport_.take_hit_request(request);
    }

    void EditorApplication::on_hit_proxy_result(const HitProxyResult& result)
    {
        scene_viewport_.receive_hit_result(world(), selection_, result);
    }

    bool EditorApplication::on_scene_viewport_extent(Extent& extent) const
    {
        return scene_viewport_.extent(extent);
    }

    void EditorApplication::on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const
    {
        scene_viewport_.build_scene_views(world(), views, extent);
    }
} // namespace toy3d
