#include "material/material_assignments.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "gamescene/actor/actor.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "material/material_asset.h"
#include "rendercore/material/material_asset_builder.h"
#include "serialization/value_codec.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        // Retain old versions until the explicit scene drain, rather than
        // releasing a Proxy while queued Remove commands still borrow it.
        constexpr std::size_t maximum_loaded_material_versions = 256u;

        Sha256Hash material_signature(const Sha256Hash& data, const ShaderMapProgram& program)
        {
            std::vector<std::uint8_t> bytes(data.begin(), data.end());
            const auto& schema = program.data().parameter_schema.schema_identity;
            bytes.insert(bytes.end(), schema.begin(), schema.end());
            bytes.insert(bytes.end(), program.data().pass_template_hash.begin(), program.data().pass_template_hash.end());
            for (const auto& stage : program.data().stages) bytes.insert(bytes.end(), stage.content_hash.begin(), stage.content_hash.end());
            return sha256(bytes);
        }

        MaterialTextureValues builtin_textures(const Material& defaults)
        {
            MaterialTextureValues textures;
            for (const auto& resource : defaults.parameter_schema().resources)
            {
                const auto found = defaults.desc().texture_defaults.find(resource.parameter_id);
                if (found != defaults.desc().texture_defaults.end()) textures.named_defaults[resource.default_value] = found->second;
            }
            return textures;
        }

        MaterialAssetData effective_root(MaterialAssetData root, const MaterialInstanceAssetData* child,
                                         const shader::ShaderParameterSchema& schema)
        {
            if (!child) return root;
            std::map<std::string, MaterialParameterOverride> values;
            for (const auto& value : root.overrides) if (material_override_matches_schema(value, schema)) values[value.name] = value;
            for (const auto& value : child->overrides) if (material_override_matches_schema(value, schema)) values[value.name] = value;
            root.overrides.clear(); for (const auto& value : values) root.overrides.push_back(value.second); return root;
        }

        bool published_identity(EditorWorkspace& workspace, const AssetLocation& location, std::string& error)
        {
            const auto index = inspect_asset(workspace.files(), location.path);
            if (!index.succeeded()) { error = index.status().message; return false; }
            if (!(index.value().asset_id == location.index.asset_id) || index.value().root_type != location.index.root_type)
            { error = "The asset identity or type has changed. Refresh Content Browser."; return false; }
            return true;
        }
    }

    void MaterialAssignments::initialize(EditorWorkspace& workspace, MaterialRef defaults)
    {
        workspace_ = &workspace;
        defaults_ = std::move(defaults);
    }

    MaterialInstanceRef MaterialAssignments::load(const AssetRef& reference, std::string& error)
    {
        if (!workspace_ || !defaults_ || !defaults_->desc().shader_program)
        { error = "The registered material Shader is unavailable."; return nullptr; }
        const auto& index = workspace_->catalog().index;
        if (!reference.asset_id.valid() || reference.subresource_id.valid() || reference.strength != AssetRefStrength::Strong ||
            (reference.expected_type != "toy3d.MaterialAssetData" && reference.expected_type != "toy3d.MaterialInstanceAssetData"))
        { error = "A slot requires a strong Material or Material Instance asset reference."; return nullptr; }
        const auto resolved = index.resolve(reference);
        if (!resolved.succeeded()) { error = resolved.message; return nullptr; }
        const auto* location = index.find(reference.asset_id);
        if (!location || !published_identity(*workspace_, *location, error)) return nullptr;
        MaterialAssetData root;
        MaterialInstanceAssetData child;
        const bool is_instance = reference.expected_type == "toy3d.MaterialInstanceAssetData";
        AssetStatus read;
        if (is_instance)
        {
            read = read_material_instance_asset(workspace_->types(), workspace_->files(), location->path, child, &index);
            if (!read.succeeded()) { error = read.message; return nullptr; }
            const auto* parent = index.find(child.parent.asset_id);
            if (!parent || !published_identity(*workspace_, *parent, error)) return nullptr;
            read = read_material_asset(workspace_->types(), workspace_->files(), parent->path, root, &index);
        }
        else read = read_material_asset(workspace_->types(), workspace_->files(), location->path, root, &index);
        if (!read.succeeded()) { error = read.message; return nullptr; }
        const auto program = program_resolver_ ? program_resolver_(root.shader_name) :
            (root.shader_name == defaults_->desc().shader_name ? defaults_->desc().shader_program : nullptr);
        if (!program) { error = "Material Shader has no registered, compiled Program. Open its source and recompile."; return nullptr; }

        // Hash typed data, including parent content, with the existing codec.
        // PNG/paths do not change rendering; no editor draft enters this signature.
        ValueWriter writer;
        auto encoded = encode_value(writer, root);
        if (encoded.succeeded() && is_instance) encoded = encode_value(writer, child);
        if (!encoded.succeeded()) { error = encoded.message; return nullptr; }
        const Sha256Hash data_signature = sha256(writer.bytes());
        const Sha256Hash signature = material_signature(data_signature, *program);
        for (const auto& loaded : loaded_)
            if (loaded.id == reference.asset_id && loaded.signature == signature) return loaded.runtime;
        if (loaded_.size() >= maximum_loaded_material_versions)
        { error = "The scene has reached its loaded material version limit (256). Restart the Editor to release old versions."; return nullptr; }

        MaterialTextureValues textures = builtin_textures(*defaults_);
        const MaterialAssetData effective = effective_root(root, is_instance ? &child : nullptr,
            material_parameter_schema_from_shader_schema(program->data().parameter_schema));
        MaterialInstanceRef runtime;
        {
            const auto candidate = create_material_from_asset(effective, program, textures);
            if (!candidate.succeeded()) { error = candidate.status().message; return nullptr; }
            runtime = candidate.value();
        }
        try { loaded_.push_back({reference.asset_id, signature, runtime, data_signature, root, child, is_instance}); }
        catch (const std::exception& exception)
        {
            MaterialInstance::release(runtime);
            error = exception.what(); return nullptr;
        }
        return runtime;
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
        MaterialInstanceRef material;
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
        discard_shader();
        if (!program || !defaults_) { error = "Material Shader candidate is incomplete."; return false; }
        if (!world_) return true;
        const auto textures = builtin_textures(*defaults_);
        for (const auto& actor_slots : assignments_)
        {
            Actor* actor = world_->find_actor_by_id(actor_slots.first);
            if (!actor) continue;
            for (const auto& assignment : actor_slots.second)
            {
                auto* component = dynamic_cast<StaticMeshComponent*>(actor->find_component_by_id(assignment.component_id));
                if (!component || !component->static_mesh()) continue;
                const auto& names = component->static_mesh()->material_slot_names();
                const auto slot = std::find(names.begin(), names.end(), assignment.slot_name);
                if (slot == names.end()) continue;
                const auto before = component->material_for_slot(static_cast<std::uint32_t>(slot - names.begin()));
                if (!before || before->material()->desc().shader_name != program->data().shader_name) continue;
                MaterialInstanceRef after;
                // A loaded revision may serve multiple slots. Prepare it once
                // while retaining each before value for rollback/FIFO lifetime.
                for (const auto& target : pending_slots_) if (target.before == before) { after = target.after; break; }
                if (!after)
                {
                    const auto loaded = std::find_if(loaded_.begin(), loaded_.end(), [&before](const LoadedMaterial& value) { return value.runtime == before; });
                    if (loaded == loaded_.end()) { error = "Scene material has no saved revision owner."; discard_shader(); return false; }
                    const auto root = effective_root(loaded->root, loaded->is_instance ? &loaded->child : nullptr,
                        material_parameter_schema_from_shader_schema(program->data().parameter_schema));
                    {
                        const auto built = create_material_from_asset(root, program, textures);
                        if (!built.succeeded()) { error = built.status().message; discard_shader(); return false; }
                        after = built.value();
                    }
                    try
                    {
                        LoadedMaterial next = *loaded; next.runtime = after;
                        next.signature = material_signature(next.data_signature, *program);
                        pending_versions_.push_back(std::move(next));
                    }
                    catch (...)
                    { MaterialInstance::release(after); throw; }
                }
                pending_slots_.push_back({actor_slots.first, assignment, before, after});
            }
        }
        if (loaded_.size() + pending_versions_.size() > maximum_loaded_material_versions)
        { error = "Loaded material revision limit reached. Restart Editor before recompiling."; discard_shader(); return false; }
        // Reserve before any Component mutation, so publication cannot allocate
        // its version owners after releasing the previous slot's strong hold.
        loaded_.reserve(loaded_.size() + pending_versions_.size());
        return true;
    }

    bool MaterialAssignments::publish_shader(std::string& error, bool defer_completion)
    {
        std::vector<std::pair<StaticMeshComponent*, std::uint32_t>> targets;
        for (const auto& slot : pending_slots_)
        {
            Actor* actor = world_ ? world_->find_actor_by_id(slot.actor_id) : nullptr;
            auto* component = actor ? dynamic_cast<StaticMeshComponent*>(actor->find_component_by_id(slot.assignment.component_id)) : nullptr;
            if (!component || !component->static_mesh()) { error = "Shader target Component changed before publication."; return false; }
            const auto& names = component->static_mesh()->material_slot_names();
            const auto found = std::find(names.begin(), names.end(), slot.assignment.slot_name);
            if (found == names.end()) { error = "Shader target slot changed before publication."; return false; }
            const auto index = static_cast<std::uint32_t>(found - names.begin());
            if (component->material_for_slot(index) != slot.before) { error = "Shader target material changed before publication."; return false; }
            targets.push_back({component, index});
        }
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            if (!targets[i].first->set_material_override(targets[i].second, pending_slots_[i].after))
            {
                discard_shader();
                error = "Component rejected a compiled material candidate."; return false;
            }
            ++published_slots_;
        }
        if (!defer_completion) complete_shader();
        return true;
    }

    void MaterialAssignments::complete_shader()
    {
        pending_slots_.clear();
        for (auto& version : pending_versions_) loaded_.push_back(std::move(version));
        pending_versions_.clear(); published_slots_ = 0u;
    }

    void MaterialAssignments::discard_shader()
    {
        // Keep candidate owners until all provisional scene changes have been
        // rolled back. The source pointer write may fail after slot preparation.
        bool rolled_back = true;
        for (std::size_t i = 0; i < published_slots_; ++i)
        {
            const auto& slot = pending_slots_[i];
            Actor* actor = world_ ? world_->find_actor_by_id(slot.actor_id) : nullptr;
            auto* component = actor ? dynamic_cast<StaticMeshComponent*>(actor->find_component_by_id(slot.assignment.component_id)) : nullptr;
            if (!component || !component->static_mesh()) continue;
            const auto& names = component->static_mesh()->material_slot_names();
            const auto found = std::find(names.begin(), names.end(), slot.assignment.slot_name);
            if (found == names.end()) continue;
            const auto index = static_cast<std::uint32_t>(found - names.begin());
            if (component->material_for_slot(index) == slot.after && !component->set_material_override(index, slot.before))
            { rolled_back = false; TOY_LOG_ERROR("Could not roll back compiled material slot {}.", slot.assignment.slot_name); }
        }
        // Even a successful rollback has enqueued material updates borrowing
        // candidate render proxies and holding their instances. Retire versions
        // at the existing scene drain rather than releasing them in this tick.
        if (published_slots_ || !rolled_back) { complete_shader(); return; }
        published_slots_ = 0u;
        pending_slots_.clear();
        for (auto& version : pending_versions_) MaterialInstance::release(version.runtime);
        pending_versions_.clear();
    }

    void MaterialAssignments::shutdown()
    {
        discard_shader(); program_resolver_ = {};
        assignments_.clear(); world_ = nullptr;
        for (auto& loaded : loaded_) MaterialInstance::release(loaded.runtime);
        loaded_.clear(); defaults_.reset(); workspace_ = nullptr;
    }
}
