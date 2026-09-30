#include "rendercore/material/material_library.h"

#include <algorithm>
#include <exception>

#include "logging/logger.h"
#include "rendercore/shader/shader_map.h"

namespace toy3d
{
    namespace
    {
        AssetStatus failure(const std::string& message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }
    }

    // --------------------------------------------------------------------------
    // MaterialLibrary: Stable asset owners and whole-graph GT publication
    // --------------------------------------------------------------------------
    MaterialLibrary::MaterialLibrary(const TypeRegistry& types, const FileSystem& files,
        std::function<const AssetIndex&()> index,
        std::function<std::shared_ptr<const ShaderMapProgram>(const std::string&)> programs,
        MaterialTextureValues textures)
        : types_(types), files_(files), index_(std::move(index)), programs_(std::move(programs)),
          textures_(std::move(textures)) {}

    AssetResult<MaterialInterfaceRef> MaterialLibrary::load(const AssetRef& reference)
    {
        const auto valid = index_().resolve(reference);
        if (!valid.succeeded()) return AssetResult<MaterialInterfaceRef>(valid);
        if (!is_material_asset_type(reference.expected_type) || reference.subresource_id.valid() ||
            reference.strength != AssetRefStrength::Strong)
            return AssetResult<MaterialInterfaceRef>(failure("Expected a strong root Material/Instance reference."));
        const auto existing = loaded_.find(reference.asset_id);
        const auto hierarchy = read_material_hierarchy(types_, files_, index_(), reference);
        if (!hierarchy.succeeded()) return AssetResult<MaterialInterfaceRef>(hierarchy.status());
        const auto program = programs_(hierarchy.value().root.shader_name);
        if (!program) return AssetResult<MaterialInterfaceRef>(failure("Material Shader has no validated published Program. Compile it first."));
        auto descriptor = material_descriptor_from_asset(hierarchy.value().root, program, textures_);
        if (!descriptor.succeeded()) return AssetResult<MaterialInterfaceRef>(descriptor.status());
        if (existing != loaded_.end()) return AssetResult<MaterialInterfaceRef>(MaterialInterfaceRef(existing->second.runtime));
        const auto ancestors = validate_loaded_ancestors(hierarchy.value(), {});
        if (!ancestors.succeeded()) return AssetResult<MaterialInterfaceRef>(ancestors);
        for (std::size_t i = 0; i < hierarchy.value().layers.size(); ++i)
        {
            const auto& layer = hierarchy.value().layers[i];
            if (loaded_.count(layer.reference.asset_id)) continue;
            auto changes = material_changes_from_overrides(layer.overrides, descriptor.value().parameter_schema, textures_);
            if (!changes.succeeded()) return AssetResult<MaterialInterfaceRef>(changes.status());
            LoadedMaterial loaded;
            loaded.reference = layer.reference;
            loaded.root = hierarchy.value().root;
            if (i == 0u)
            {
                loaded.root = hierarchy.value().root;
                loaded.runtime = Material::create(descriptor.value());
            }
            else
            {
                loaded.instance.parent = hierarchy.value().layers[i - 1u].reference;
                loaded.instance.overrides = layer.overrides;
                loaded.runtime = MaterialInstance::create(loaded_.at(loaded.instance.parent.asset_id).runtime);
            }
            if (!loaded.runtime) return AssetResult<MaterialInterfaceRef>(failure("Could not create Material hierarchy."));
            try
            {
                if (!changes.value().empty() && !loaded.runtime->publish_tree(loaded.runtime->desc(), changes.value()))
                    return AssetResult<MaterialInterfaceRef>(failure("Could not publish Material parameters."));
                loaded_.emplace(layer.reference.asset_id, std::move(loaded));
            }
            catch (const std::exception& error)
            {
                if (loaded.runtime) loaded.runtime->retire_proxy();
                return AssetResult<MaterialInterfaceRef>(failure(error.what()));
            }
        }
        return AssetResult<MaterialInterfaceRef>(MaterialInterfaceRef(loaded_.at(reference.asset_id).runtime));
    }

    AssetResult<MaterialInstanceRef> MaterialLibrary::create_instance(MaterialInterfaceRef parent)
    {
        bool owned = false;
        for (const auto& item : loaded_) if (item.second.runtime == parent) owned = true;
        for (const auto& item : temporary_) if (item == parent) owned = true;
        if (!owned) return AssetResult<MaterialInstanceRef>(failure("Parent must belong to this MaterialLibrary."));
        auto instance = MaterialInstance::create(std::move(parent));
        if (!instance) return AssetResult<MaterialInstanceRef>(failure("Could not create independent MaterialInstance."));
        temporary_.push_back(instance);
        return AssetResult<MaterialInstanceRef>(std::move(instance));
    }

    AssetStatus MaterialLibrary::add_configuration(LoadedMaterial& loaded, const MaterialAssetData& root,
        const MaterialInstanceAssetData& instance, std::shared_ptr<const ShaderMapProgram> program,
        MaterialInterfaceRef parent)
    {
        auto descriptor = material_descriptor_from_asset(root, std::move(program), textures_);
        if (!descriptor.succeeded()) return descriptor.status();
        const bool child = loaded.reference.expected_type == "toy3d.MaterialInstanceAssetData";
        auto changes = material_changes_from_overrides(child ? instance.overrides : root.overrides,
            descriptor.value().parameter_schema, textures_);
        if (!changes.succeeded()) return changes.status();
        pending_.push_back({loaded.runtime.get(), descriptor.value(), changes.value(), std::move(parent)});
        previous_.push_back({loaded.runtime.get(), loaded.runtime->desc(), loaded.runtime->local_overrides_, loaded.runtime->parent()});
        pending_data_[loaded.reference.asset_id] = {root, instance};
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::release_instance(MaterialInstanceRef& instance)
    {
        const auto found = std::find(temporary_.begin(), temporary_.end(), instance);
        if (found == temporary_.end()) return failure("Instance does not belong to this MaterialLibrary.");
        if (instance.use_count() != 2)
            return failure("Remove scene/child users and drain their commands before releasing the instance.");
        temporary_.erase(found);
        try { MaterialInstance::release(instance); }
        catch (const std::exception& error)
        {
            temporary_.push_back(instance); // erase retained the required capacity.
            return failure(error.what());
        }
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::validate_loaded_ancestors(const MaterialAssetHierarchy& hierarchy,
        const std::vector<AssetId>& updating) const
    {
        // A saved ancestor can differ from its still-active configuration
        // after an external edit or a failed publication. Do not report a
        // successful child publication while silently inheriting old data.
        for (std::size_t i = 0; i + 1u < hierarchy.layers.size(); ++i)
        {
            const auto& layer = hierarchy.layers[i];
            const auto ancestor = loaded_.find(layer.reference.asset_id);
            if (ancestor == loaded_.end()) continue;
            const bool publishing = std::find(updating.begin(), updating.end(), layer.reference.asset_id) != updating.end();
            if (publishing) continue;
            ValueWriter saved;
            ValueWriter active;
            ValueStatus encoded;
            if (i == 0u)
            {
                encoded = encode_value(saved, hierarchy.root);
                if (encoded.succeeded()) encoded = encode_value(active, ancestor->second.root);
            }
            else
            {
                MaterialInstanceAssetData parent_data;
                parent_data.parent = hierarchy.layers[i - 1u].reference;
                parent_data.overrides = layer.overrides;
                encoded = encode_value(saved, parent_data);
                if (encoded.succeeded()) encoded = encode_value(active, ancestor->second.instance);
            }
            if (!encoded.succeeded()) return failure(encoded.message);
            if (saved.bytes() != active.bytes())
            {
                return failure("Parent has saved changes that are not published. Reload the Parent first: " + layer.reference.asset_id.hex());
            }
        }
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::prepare(const AssetRef& changed)
    {
        discard();
        const auto existing = load(changed);
        if (!existing.succeeded()) return existing.status();
        std::vector<AssetId> affected;
        for (const auto& item : loaded_)
        {
            for (auto current = MaterialInterfaceRef(item.second.runtime); current; current = current->parent())
                if (current == existing.value()) { affected.push_back(item.first); break; }
        }
        for (const auto& id : affected)
        {
            auto& loaded = loaded_.at(id);
            const auto hierarchy = read_material_hierarchy(types_, files_, index_(), loaded.reference);
            if (!hierarchy.succeeded()) { discard(); return hierarchy.status(); }
            const auto ancestors = validate_loaded_ancestors(hierarchy.value(), affected);
            if (!ancestors.succeeded()) { discard(); return ancestors; }
            MaterialInstanceAssetData child;
            MaterialInterfaceRef parent;
            if (hierarchy.value().layers.size() > 1u)
            {
                const auto& layers = hierarchy.value().layers;
                child.parent = layers[layers.size() - 2u].reference;
                child.overrides = layers.back().overrides;
                const auto source = load(child.parent);
                if (!source.succeeded()) { discard(); return source.status(); }
                parent = source.value();
            }
            const auto status = add_configuration(loaded, hierarchy.value().root, child,
                programs_(hierarchy.value().root.shader_name), std::move(parent));
            if (!status.succeeded()) { discard(); return status; }
        }
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::reload(const AssetRef& reference)
    {
        auto status = prepare(reference);
        if (!status.succeeded()) return status;
        return publish();
    }

    AssetStatus MaterialLibrary::prepare_shader(std::shared_ptr<const ShaderMapProgram> program)
    {
        discard();
        if (!program) return failure("Material Shader candidate is missing.");
        for (auto& item : loaded_)
        {
            auto& loaded = item.second;
            if (loaded.runtime->desc().shader_name != program->data().shader_name) continue;
            const auto status = add_configuration(loaded, loaded.root, loaded.instance, program, loaded.runtime->parent());
            if (!status.succeeded()) { discard(); return status; }
        }
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::publish(bool defer_completion)
    {
        try
        {
            if (!pending_.empty() && !MaterialInterface::publish_configurations(pending_))
                return failure("Material candidate graph could not be resolved. Active values are unchanged.");
            published_ = true;
            if (!defer_completion) complete();
            return AssetStatus::success();
        }
        catch (const std::exception& error) { return failure(error.what()); }
    }

    void MaterialLibrary::complete()
    {
        for (auto& item : pending_data_)
        {
            auto& loaded = loaded_.at(item.first);
            loaded.root = std::move(item.second.first);
            loaded.instance = std::move(item.second.second);
        }
        pending_.clear(); previous_.clear(); pending_data_.clear(); published_ = false;
    }

    void MaterialLibrary::discard()
    {
        if (published_)
        {
            try
            {
                if (!MaterialInterface::publish_configurations(previous_))
                    TOY_LOG_ERROR("Could not restore previous Material configurations.");
            }
            catch (const std::exception& error) { TOY_LOG_ERROR("Material rollback failed: {}", error.what()); }
        }
        pending_.clear(); previous_.clear(); pending_data_.clear(); published_ = false;
    }

    void MaterialLibrary::shutdown()
    {
        discard();
        // Every stable proxy is retired before destroying any node. Shared
        // Parent references therefore cannot reorder proxy ownership transfers.
        for (auto it = temporary_.rbegin(); it != temporary_.rend(); ++it) (*it)->retire_proxy();
        for (auto& item : loaded_) item.second.runtime->retire_proxy();
        temporary_.clear(); loaded_.clear(); textures_ = {};
    }
}
