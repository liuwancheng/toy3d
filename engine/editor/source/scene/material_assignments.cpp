#include "scene/material_assignments.h"

#include <algorithm>
#include <utility>

#include "gamescene/actor/actor.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/world/world.h"
#include "workspace/editor_workspace.h"
#include "scene/editor_command_history.h"
#include "shader/shader_workflow.h"
#include "logging/logger.h"

namespace toy3d
{
    void MaterialAssignments::initialize(EditorWorkspace& workspace, MaterialLibrary& library)
    {
        workspace_ = &workspace;
        library_ = &library;
    }

    MaterialInterfaceRef MaterialAssignments::load(const AssetRef& reference, std::string& error)
    {
        if (!library_) { error = "MaterialLibrary is unavailable."; return {}; }
        const auto result = library_->load(reference);
        if (!result.succeeded()) { error = result.status().message; return {}; }
        return result.value();
    }

    AssetStatus MaterialAssignments::reload(const AssetRef& reference)
    {
        return library_->reload(reference);
    }

    bool MaterialAssignments::assign(World& world, std::uint32_t actor_id,
                                     const MaterialSlotAssignment& assignment, std::string& error)
    {
        error.clear();
        Actor* actor = world.find_actor_by_id(actor_id);
        auto* component = actor ? dynamic_cast<StaticMeshComponent*>(actor->find_component_by_id(assignment.component_id)) : nullptr;
        if (!component || !component->static_mesh())
        { error = "The target StaticMesh Component no longer exists."; return false; }
        const auto& names = component->static_mesh()->material_slot_names();
        const auto found = std::find(names.begin(), names.end(), assignment.slot_name);
        if (found == names.end()) { error = "The target material slot no longer exists: " + assignment.slot_name; return false; }
        const auto slot = static_cast<std::uint32_t>(found - names.begin());
        MaterialInterfaceRef material;
        if (assignment.material.asset_id.valid())
        {
            material = load(assignment.material, error);
            if (!material) return false;
        }
        else if (assignment.material.subresource_id.valid() || !assignment.material.expected_type.empty() ||
                 assignment.material.strength != AssetRefStrength::Strong)
        { error = "A default material assignment must have an empty reference."; return false; }
        if (world_ != &world) { assignments_.clear(); world_ = &world; }
        // All asset/slot preparation finishes before the Component mutates its
        // GT state and sends the complete material list through the render bridge.
        const bool applied = material ? component->set_material_override(slot, material) : component->clear_material_override(slot);
        if (!applied) { error = "The Component rejected its material assignment."; return false; }
        auto& values = assignments_[actor_id];
        values.erase(std::remove_if(values.begin(), values.end(), [&assignment](const MaterialSlotAssignment& value)
        { return value.component_id == assignment.component_id && value.slot_name == assignment.slot_name; }), values.end());
        if (material) values.push_back(assignment);
        if (values.empty()) assignments_.erase(actor_id);
        return true;
    }

    AssetRef MaterialAssignments::reference(const World& world, std::uint32_t actor_id,
        std::uint32_t component_id, const std::string& slot_name) const
    {
        if (world_ != &world) return {};
        const auto found = assignments_.find(actor_id);
        if (found != assignments_.end())
            for (const auto& value : found->second)
                if (value.component_id == component_id && value.slot_name == slot_name) return value.material;
        return {};
    }

    std::vector<MaterialSlotAssignment> MaterialAssignments::capture(const World& world, std::uint32_t actor_id) const
    {
        if (world_ != &world) return {};
        const auto found = assignments_.find(actor_id);
        return found == assignments_.end() ? std::vector<MaterialSlotAssignment>{} : found->second;
    }

    void MaterialAssignments::forget(std::uint32_t actor_id) { assignments_.erase(actor_id); }

    bool MaterialAssignments::prepare_shader(const ShaderMapProgramRef& program, std::string& error)
    {
        if (!library_) { error = "MaterialLibrary is unavailable."; return false; }
        const auto status = library_->prepare_shader(program);
        if (!status.succeeded()) error = status.message;
        return status.succeeded();
    }

    bool MaterialAssignments::publish_shader(std::string& error, bool defer_completion)
    {
        const auto status = library_->publish(defer_completion);
        if (!status.succeeded()) error = status.message;
        return status.succeeded();
    }

    void MaterialAssignments::complete_shader() { library_->complete(); }
    void MaterialAssignments::discard_shader() { if (library_) library_->discard(); }
    void MaterialAssignments::shutdown()
    {
        discard_shader();
        assignments_.clear(); world_ = nullptr; workspace_ = nullptr; library_ = nullptr;
        pending_assignment_ = {}; shaders_ = nullptr;
    }

    bool MaterialAssignments::offer_compile_assignment(World& world, std::uint32_t actor_id, const MaterialSlotAssignment& assignment)
    {
        if (!shaders_ || pending_assignment_.compiling) return false;
        pending_assignment_ = {};
        const auto hierarchy = read_material_hierarchy(workspace_->types(), workspace_->files(), workspace_->catalog().index, assignment.material);
        if (!hierarchy.succeeded()) return false;
        const auto* shader = shaders_->find(hierarchy.value().root.shader_name);
        if (!shader || shader->usage != BuiltinShaderUsage::Material || shader->program) return false;
        auto* actor = world.find_actor_by_id(actor_id);
        auto* mesh = actor ? dynamic_cast<StaticMeshComponent*>(actor->find_component_by_id(assignment.component_id)) : nullptr;
        if (!mesh || !mesh->static_mesh()) return false;
        const auto& names = mesh->static_mesh()->material_slot_names();
        const auto slot = std::find(names.begin(), names.end(), assignment.slot_name);
        if (slot == names.end()) return false;
        PendingAssignment pending;
        pending.world = &world; pending.generation = world.scene_generation(); pending.actor_id = actor_id;
        pending.assignment = assignment; pending.mesh = mesh->static_mesh();
        pending.previous = mesh->material_for_slot(static_cast<std::uint32_t>(slot - names.begin()));
        pending.shader = shader->name;
        for (const auto& layer : hierarchy.value().layers)
        {
            const auto* location = workspace_->catalog().index.find(layer.reference.asset_id);
            if (!location) return false;
            const auto bytes = workspace_->files().read_binary(location->path);
            if (!bytes.succeeded()) return false;
            pending.descriptions.emplace(layer.reference.asset_id, sha256(bytes.value()));
        }
        pending_assignment_ = std::move(pending);
        return true;
    }

    bool MaterialAssignments::can_compile_assignment() const
    {
        return shaders_ && pending_assignment_.world && !pending_assignment_.compiling && !shaders_->busy();
    }

    bool MaterialAssignments::compile_assignment(std::string& error)
    {
        if (!can_compile_assignment()) { error = "Shader compilation is unavailable or busy."; return false; }
        if (!shaders_->recompile(pending_assignment_.shader)) { error = shaders_->error(); return false; }
        pending_assignment_.compiling = true; error.clear(); return true;
    }

    void MaterialAssignments::tick_compile_assignment(World& world, EditorCommandHistory& history, std::string& error)
    {
        auto& pending = pending_assignment_;
        if (!pending.compiling || !shaders_ || shaders_->busy() || history.active()) return;
        auto* actor = world.find_actor_by_id(pending.actor_id);
        auto* mesh = actor ? dynamic_cast<StaticMeshComponent*>(actor->find_component_by_id(pending.assignment.component_id)) : nullptr;
        bool valid = pending.world == &world && pending.generation == world.scene_generation() && mesh && mesh->static_mesh() == pending.mesh;
        if (valid)
        {
            const auto& names = pending.mesh->material_slot_names();
            const auto slot = std::find(names.begin(), names.end(), pending.assignment.slot_name);
            valid = slot != names.end() && mesh->material_for_slot(static_cast<std::uint32_t>(slot - names.begin())) == pending.previous;
        }
        for (const auto& description : pending.descriptions)
        {
            const auto* location = workspace_->catalog().index.find(description.first);
            if (!location) { valid = false; break; }
            const auto bytes = workspace_->files().read_binary(location->path);
            if (!bytes.succeeded() || sha256(bytes.value()) != description.second) { valid = false; break; }
        }
        if (valid)
        {
            // Revalidate paired meta identities as well as description bytes;
            // an asynchronous compile must not apply a replaced asset hierarchy.
            const auto hierarchy = read_material_hierarchy(workspace_->types(), workspace_->files(),
                workspace_->catalog().index, pending.assignment.material);
            valid = hierarchy.succeeded() && hierarchy.value().root.shader_name == pending.shader;
        }
        if (!valid) { error = "Compile and Assign cancelled: target slot, scene or Material asset changed."; TOY_LOG_WARN("{}", error); }
        else if (!shaders_->program(pending.shader))
        { error = shaders_->unavailable_reason(pending.shader); TOY_LOG_WARN("Compile and Assign did not apply: {}", error); }
        else if (!history.assign_material(world, pending.actor_id, pending.assignment.component_id, pending.assignment.slot_name, pending.assignment.material, error))
            TOY_LOG_ERROR("Compile and Assign [Actor {} Component {} slot '{}']: {}", pending.actor_id, pending.assignment.component_id, pending.assignment.slot_name, error);
        pending = {};
    }
}
