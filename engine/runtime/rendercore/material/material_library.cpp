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
    } // namespace

    // --------------------------------------------------------------------------
    // MaterialLibrary: Stable asset owners and whole-graph GT publication
    // --------------------------------------------------------------------------
    MaterialLibrary::MaterialLibrary(const TypeRegistry& types, const FileSystem& files,
                                     std::function<const AssetIndex&()> index, TextureResolver load_texture,
                                     std::function<ShaderMapCollectionRef(
                                         const std::string&, const std::vector<shader::ShaderPermutationSelection>&)>
                                         programs,
                                     MaterialTextureValues textures)
        : types_(types), files_(files), index_(std::move(index)), load_texture_(std::move(load_texture)),
          programs_(std::move(programs)), textures_(std::move(textures))
    {
    }

    AssetStatus MaterialLibrary::ensure_textures(const std::vector<MaterialParameterOverride>& overrides)
    {
        for (const auto& item : overrides)
        {
            // C++17 get_if selects only persisted Texture2D references.
            const auto* reference = std::get_if<AssetRef>(&item.value);
            if (!reference || textures_.assets.count(reference->asset_id))
            {
                continue;
            }
            std::string error;
            TextureRef texture = load_texture_ ? load_texture_(*reference, error) : TextureRef{};
            if (!texture)
            {
                return failure(error.empty() ? "Material texture could not be loaded." : error);
            }
            textures_.assets.emplace(reference->asset_id, std::move(texture));
        }
        return AssetStatus::success();
    }

    AssetResult<MaterialInterfaceRef> MaterialLibrary::load(const AssetRef& reference)
    {
        const auto valid = index_().resolve(reference);
        if (!valid.succeeded())
        {
            return AssetResult<MaterialInterfaceRef>(valid);
        }
        if (!is_material_asset_type(reference.expected_type) || reference.subresource_id.valid() ||
            reference.strength != AssetRefStrength::Strong)
        {
            return AssetResult<MaterialInterfaceRef>(failure("Expected a strong root Material/Instance reference."));
        }
        const auto existing = loaded_.find(reference.asset_id);
        const auto hierarchy = read_material_hierarchy(types_, files_, index_(), reference);
        if (!hierarchy.succeeded())
        {
            return AssetResult<MaterialInterfaceRef>(hierarchy.status());
        }
        for (const auto& layer : hierarchy.value().layers)
        {
            const auto textures = ensure_textures(layer.overrides);
            if (!textures.succeeded())
            {
                return AssetResult<MaterialInterfaceRef>(textures);
            }
        }
        MaterialAssetData selected_root = hierarchy.value().root;
        selected_root.static_options = hierarchy.value().effective_static_options();
        const auto selected_program =
            programs_(selected_root.shader_name, material_static_selections(selected_root.static_options));
        if (!selected_program)
        {
            const auto* location = index_().find(reference.asset_id);
            const std::string reason = shader_diagnostic_
                                           ? shader_diagnostic_(selected_root.shader_name)
                                           : "The exact static configuration of Shader '" + selected_root.shader_name +
                                                 "' has no validated ShaderMap. Compile it first.";
            return AssetResult<MaterialInterfaceRef>(
                failure("Material " + (location ? location->path.utf8() : reference.asset_id.hex()) + ": " + reason));
        }
        const auto default_textures = resolve_builtin_material_texture_defaults(
            files_, index_(), selected_program->programs().front()->data().parameter_schema, textures_);
        if (!default_textures.succeeded())
        {
            return AssetResult<MaterialInterfaceRef>(default_textures);
        }
        auto descriptor = material_descriptor_from_asset(selected_root, selected_program, textures_);
        if (!descriptor.succeeded())
        {
            return AssetResult<MaterialInterfaceRef>(descriptor.status());
        }
        if (existing != loaded_.end())
        {
            return AssetResult<MaterialInterfaceRef>(MaterialInterfaceRef(existing->second.runtime));
        }
        const auto ancestors = validate_loaded_ancestors(hierarchy.value(), {});
        if (!ancestors.succeeded())
        {
            return AssetResult<MaterialInterfaceRef>(ancestors);
        }
        std::vector<MaterialDesc> descriptors;
        MaterialAssetHierarchy prefix;
        for (const auto& layer : hierarchy.value().layers)
        {
            prefix.layers.push_back(layer);
            auto data = hierarchy.value().root;
            data.static_options = prefix.effective_static_options();
            const auto selected = programs_(data.shader_name, material_static_selections(data.static_options));
            auto desc = material_descriptor_from_asset(data, selected, textures_);
            if (!desc.succeeded())
            {
                return AssetResult<MaterialInterfaceRef>(desc.status());
            }
            auto authored = desc.value();
            authored.static_options = layer.static_options;
            descriptors.push_back(std::move(authored));
        }
        for (std::size_t i = 0; i < hierarchy.value().layers.size(); ++i)
        {
            const auto& layer = hierarchy.value().layers[i];
            if (loaded_.count(layer.reference.asset_id))
            {
                continue;
            }
            auto changes =
                material_changes_from_overrides(layer.overrides, descriptor.value().parameter_schema, textures_);
            if (!changes.succeeded())
            {
                return AssetResult<MaterialInterfaceRef>(changes.status());
            }
            LoadedMaterial loaded;
            loaded.reference = layer.reference;
            loaded.root = hierarchy.value().root;
            if (i == 0u)
            {
                loaded.root = hierarchy.value().root;
                loaded.runtime = Material::create(descriptors[i]);
            }
            else
            {
                loaded.instance.parent = hierarchy.value().layers[i - 1u].reference;
                loaded.instance.overrides = layer.overrides;
                loaded.instance.static_options = layer.static_options;
                loaded.runtime = MaterialInstance::create(loaded_.at(loaded.instance.parent.asset_id).runtime,
                                                          descriptors[i].shader_map, layer.static_options);
            }
            if (!loaded.runtime)
            {
                return AssetResult<MaterialInterfaceRef>(failure("Could not create Material hierarchy."));
            }
            try
            {
                MaterialInterface::Configuration configuration{loaded.runtime.get(), descriptors[i], changes.value(),
                                                               loaded.runtime->parent(), true};
                if (!MaterialInterface::publish_configurations({std::move(configuration)}))
                {
                    return AssetResult<MaterialInterfaceRef>(failure("Could not publish Material parameters."));
                }
                loaded_.emplace(layer.reference.asset_id, std::move(loaded));
            }
            catch (const std::exception& error)
            {
                if (loaded.runtime)
                {
                    loaded.runtime->retire_proxy();
                }
                return AssetResult<MaterialInterfaceRef>(failure(error.what()));
            }
        }
        return AssetResult<MaterialInterfaceRef>(MaterialInterfaceRef(loaded_.at(reference.asset_id).runtime));
    }

    AssetResult<MaterialInstanceRef> MaterialLibrary::create_instance(MaterialInterfaceRef parent)
    {
        bool owned = false;
        for (const auto& item : loaded_)
        {
            if (item.second.runtime == parent)
            {
                owned = true;
            }
        }
        for (const auto& item : temporary_)
        {
            if (item == parent)
            {
                owned = true;
            }
        }
        if (!owned)
        {
            return AssetResult<MaterialInstanceRef>(failure("Parent must belong to this MaterialLibrary."));
        }
        auto instance = MaterialInstance::create(std::move(parent));
        if (!instance)
        {
            return AssetResult<MaterialInstanceRef>(failure("Could not create independent MaterialInstance."));
        }
        temporary_.push_back(instance);
        return AssetResult<MaterialInstanceRef>(std::move(instance));
    }

    AssetStatus MaterialLibrary::add_configuration(LoadedMaterial& loaded, const MaterialAssetData& root,
                                                   const MaterialInstanceAssetData& instance,
                                                   ShaderMapCollectionRef program, MaterialInterfaceRef parent)
    {
        const auto root_textures = ensure_textures(root.overrides);
        if (!root_textures.succeeded())
        {
            return root_textures;
        }
        const auto child_textures = ensure_textures(instance.overrides);
        if (!child_textures.succeeded())
        {
            return child_textures;
        }
        if (!program)
        {
            return failure("The selected material static configuration has no validated ShaderMap.");
        }
        auto selected = root;
        selected.static_options = material_static_options(program->index().material_selections);
        const auto default_textures = resolve_builtin_material_texture_defaults(
            files_, index_(), program->programs().front()->data().parameter_schema, textures_);
        if (!default_textures.succeeded())
        {
            return default_textures;
        }
        auto descriptor = material_descriptor_from_asset(selected, std::move(program), textures_);
        if (!descriptor.succeeded())
        {
            return descriptor.status();
        }
        const bool child = loaded.reference.expected_type == "toy3d.MaterialInstanceAssetData";
        auto authored = descriptor.value();
        authored.static_options = child ? instance.static_options : root.static_options;
        auto changes = material_changes_from_overrides(child ? instance.overrides : root.overrides,
                                                       descriptor.value().parameter_schema, textures_);
        if (!changes.succeeded())
        {
            return changes.status();
        }
        pending_.push_back({loaded.runtime.get(), std::move(authored), changes.value(), std::move(parent), true});
        previous_.push_back(
            {loaded.runtime.get(), loaded.runtime->desc(), loaded.runtime->local_overrides_, loaded.runtime->parent()});
        pending_data_[loaded.reference.asset_id] = {root, instance};
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::release_instance(MaterialInstanceRef& instance)
    {
        const auto found = std::find(temporary_.begin(), temporary_.end(), instance);
        if (found == temporary_.end())
        {
            return failure("Instance does not belong to this MaterialLibrary.");
        }
        if (instance.use_count() != 2)
        {
            return failure("Remove scene/child users and drain their commands before releasing the instance.");
        }
        temporary_.erase(found);
        try
        {
            MaterialInstance::release(instance);
        }
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
            if (ancestor == loaded_.end())
            {
                continue;
            }
            const bool publishing =
                std::find(updating.begin(), updating.end(), layer.reference.asset_id) != updating.end();
            if (publishing)
            {
                continue;
            }
            ValueWriter saved;
            ValueWriter active;
            ValueStatus encoded;
            if (i == 0u)
            {
                encoded = encode_value(saved, hierarchy.root);
                if (encoded.succeeded())
                {
                    encoded = encode_value(active, ancestor->second.root);
                }
            }
            else
            {
                MaterialInstanceAssetData parent_data;
                parent_data.parent = hierarchy.layers[i - 1u].reference;
                parent_data.overrides = layer.overrides;
                parent_data.static_options = layer.static_options;
                encoded = encode_value(saved, parent_data);
                if (encoded.succeeded())
                {
                    encoded = encode_value(active, ancestor->second.instance);
                }
            }
            if (!encoded.succeeded())
            {
                return failure(encoded.message);
            }
            if (saved.bytes() != active.bytes())
            {
                return failure("Parent has saved changes that are not published. Reload the Parent first: " +
                               layer.reference.asset_id.hex());
            }
        }
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::prepare(const AssetRef& changed)
    {
        discard();
        const auto existing = load(changed);
        if (!existing.succeeded())
        {
            return existing.status();
        }
        std::vector<AssetId> affected;
        for (const auto& item : loaded_)
        {
            for (auto current = MaterialInterfaceRef(item.second.runtime); current; current = current->parent())
            {
                if (current == existing.value())
                {
                    affected.push_back(item.first);
                    break;
                }
            }
        }
        for (const auto& id : affected)
        {
            auto& loaded = loaded_.at(id);
            const auto hierarchy = read_material_hierarchy(types_, files_, index_(), loaded.reference);
            if (!hierarchy.succeeded())
            {
                discard();
                return hierarchy.status();
            }
            const auto ancestors = validate_loaded_ancestors(hierarchy.value(), affected);
            if (!ancestors.succeeded())
            {
                discard();
                return ancestors;
            }
            MaterialInstanceAssetData child;
            MaterialInterfaceRef parent;
            if (hierarchy.value().layers.size() > 1u)
            {
                const auto& layers = hierarchy.value().layers;
                child.parent = layers[layers.size() - 2u].reference;
                child.overrides = layers.back().overrides;
                child.static_options = layers.back().static_options;
                const auto source = load(child.parent);
                if (!source.succeeded())
                {
                    discard();
                    return source.status();
                }
                parent = source.value();
            }
            auto root = hierarchy.value().root;
            root.static_options = hierarchy.value().effective_static_options();
            const auto selected = programs_(root.shader_name, material_static_selections(root.static_options));
            const auto status = add_configuration(loaded, hierarchy.value().root, child, selected, std::move(parent));
            if (!status.succeeded())
            {
                discard();
                return status;
            }
        }
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::reload(const AssetRef& reference)
    {
        auto status = prepare(reference);
        if (!status.succeeded())
        {
            return status;
        }
        return publish();
    }

    AssetStatus MaterialLibrary::prepare_shader(ShaderMapCollectionRef program)
    {
        return prepare_shader(std::vector<ShaderMapCollectionRef>{std::move(program)});
    }

    AssetStatus MaterialLibrary::prepare_shader(const std::vector<ShaderMapCollectionRef>& programs)
    {
        discard();
        if (programs.empty() || programs.size() > shader::max_shader_compile_source_programs || !programs.front())
        {
            return failure("Material Shader candidate configuration set is missing or oversized.");
        }
        const auto& reference = programs.front()->index();
        const auto domain = shader::serialize_shader_permutation_domain(reference.material_domain);
        const auto material_schema =
            material_parameter_schema_from_shader_schema(programs.front()->programs().front()->data().parameter_schema)
                .schema_identity;
        std::map<Sha256Hash, ShaderMapCollectionRef> configurations;
        std::size_t program_count = 0u;
        for (const auto& program : programs)
        {
            if (!program)
            {
                return failure("Material Shader candidate configuration is null.");
            }
            const auto& index = program->index();
            program_count += program->programs().size();
            if (index.shader_name != reference.shader_name || index.source_hash != reference.source_hash ||
                index.target != reference.target || index.profile != reference.profile ||
                shader::serialize_shader_permutation_domain(index.material_domain) != domain ||
                material_parameter_schema_from_shader_schema(program->programs().front()->data().parameter_schema)
                        .schema_identity != material_schema ||
                program_count > shader::max_shader_compile_source_programs ||
                !configurations.emplace(index.permutation_key, program).second)
            {
                return failure("Material Shader candidate must contain unique configurations of one source revision, "
                               "profile, domain and complete Material schema within the source budget.");
            }
            const auto& policy = index.policy;
            const auto& expected = reference.policy;
            bool features_match = index.features.size() == reference.features.size();
            for (std::size_t i = 0; features_match && i < index.features.size(); ++i)
            {
                features_match = index.features[i].feature == reference.features[i].feature &&
                                 shader::serialize_shader_static_condition(index.features[i].condition) ==
                                     shader::serialize_shader_static_condition(reference.features[i].condition);
            }
            if (!features_match || policy.target != expected.target || policy.profile != expected.profile ||
                policy.editor != expected.editor || policy.vertex_factory_support != expected.vertex_factory_support ||
                policy.allow_pcf != expected.allow_pcf || policy.allow_sky != expected.allow_sky ||
                policy.capabilities != expected.capabilities ||
                index.standard_tangent_input != reference.standard_tangent_input ||
                index.declares_tangent_frame != reference.declares_tangent_frame ||
                shader::serialize_shader_static_condition(index.tangent_frame_when) !=
                    shader::serialize_shader_static_condition(reference.tangent_frame_when) ||
                shader::serialize_shader_static_condition(index.supported_when) !=
                    shader::serialize_shader_static_condition(reference.supported_when))
            {
                return failure("Material Shader candidate configurations disagree on source feature or build policy.");
            }
        }
        const auto select = [&](const std::vector<MaterialStaticOption>& options,
                                ShaderMapCollectionRef& selected) -> AssetStatus
        {
            const auto resolved =
                shader::resolve_shader_permutation(reference.material_domain, material_static_selections(options));
            if (!resolved.succeeded())
            {
                return failure(resolved.errors.front().message);
            }
            const auto found = configurations.find(resolved.permutation->key);
            if (found == configurations.end())
            {
                return failure("Material Shader candidate is missing required static configuration " +
                               sha256_to_hex(resolved.permutation->key) + " of " + reference.shader_name);
            }
            selected = found->second;
            return AssetStatus::success();
        };
        if (default_material_ && default_material_->desc().shader_name == reference.shader_name)
        {
            ShaderMapCollectionRef program;
            auto selected = select({}, program);
            if (!selected.succeeded())
            {
                return selected;
            }
            MaterialAssetData data;
            data.shader_name = reference.shader_name;
            data.two_sided = default_material_->desc().two_sided;
            const auto default_textures = resolve_builtin_material_texture_defaults(
                files_, index_(), program->programs().front()->data().parameter_schema, textures_);
            if (!default_textures.succeeded())
            {
                return default_textures;
            }
            const auto descriptor = material_descriptor_from_asset(data, program, textures_);
            if (!descriptor.succeeded())
            {
                return descriptor.status();
            }
            auto* target = const_cast<Material*>(default_material_.get());
            pending_.push_back({target, descriptor.value(), target->local_overrides_, {}, true});
            previous_.push_back({target, target->desc_, target->local_overrides_, {}});
        }
        // Resolve every saved hierarchy against the new declaration/defaults.
        // Looking up old hashes would silently strand instances when defaults or
        // dimension identities change. No live node changes during preparation.
        for (auto& item : loaded_)
        {
            auto& loaded = item.second;
            if (loaded.runtime->desc().shader_name != reference.shader_name)
            {
                continue;
            }
            const auto hierarchy = read_material_hierarchy(types_, files_, index_(), loaded.reference);
            if (!hierarchy.succeeded() || hierarchy.value().root.shader_name != reference.shader_name)
            {
                discard();
                return hierarchy.succeeded() ? failure("Material Shader identity changed during candidate preparation.")
                                             : hierarchy.status();
            }
            ShaderMapCollectionRef program;
            auto status = select(hierarchy.value().effective_static_options(), program);
            if (status.succeeded())
            {
                MaterialInstanceAssetData instance;
                MaterialInterfaceRef parent;
                if (hierarchy.value().layers.size() > 1u)
                {
                    const auto& layers = hierarchy.value().layers;
                    instance.parent = layers[layers.size() - 2u].reference;
                    instance.overrides = layers.back().overrides;
                    instance.static_options = layers.back().static_options;
                    const auto owner = loaded_.find(instance.parent.asset_id);
                    if (owner == loaded_.end())
                    {
                        status = failure("Material candidate Parent must be loaded before source publication.");
                    }
                    else
                    {
                        parent = owner->second.runtime;
                    }
                }
                if (status.succeeded())
                {
                    status = add_configuration(loaded, hierarchy.value().root, instance, program, std::move(parent));
                }
            }
            if (!status.succeeded())
            {
                discard();
                return status;
            }
        }
        pending_shader_family_ = programs;
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::collect_shader_validation_targets(
        const std::vector<ShaderMapCollectionRef>& programs, std::vector<MaterialShaderMapValidationTarget>& targets)
    {
        auto status = prepare_shader(programs);
        if (!status.succeeded())
        {
            return status;
        }
        std::vector<MaterialInterface::PreparedConfiguration> revisions;
        std::vector<MaterialInstanceRef> owners;
        if (!MaterialInterface::prepare_configurations(pending_, programs, revisions, owners))
        {
            discard();
            return failure("Material candidate graph has a missing inherited static configuration.");
        }
        for (const auto& revision : revisions)
        {
            targets.push_back({revision.destination, revision.configuration.descriptor.shader_map});
        }
        discard();
        return AssetStatus::success();
    }

    AssetStatus MaterialLibrary::publish(bool defer_completion)
    {
        try
        {
            if (!pending_.empty())
            {
                std::vector<MaterialInterface::PreparedConfiguration> revisions;
                std::vector<MaterialInstanceRef> owners;
                if (!MaterialInterface::prepare_configurations(pending_, pending_shader_family_, revisions, owners))
                {
                    return failure("Material candidate graph could not be resolved. Active values are unchanged.");
                }
                // Rollback includes temporary and unmanaged descendants too;
                // their old source revision cannot be reconstructed from a key.
                previous_.clear();
                for (const auto& revision : revisions)
                {
                    auto* target = revision.configuration.target;
                    previous_.push_back({target, target->desc_, target->local_overrides_, target->parent(), true});
                }
                if (!MaterialInterface::publish_configurations(pending_, pending_shader_family_))
                {
                    return failure("Material candidate graph could not be published. Active values are unchanged.");
                }
            }
            published_ = true;
            if (!defer_completion)
            {
                complete();
            }
            return AssetStatus::success();
        }
        catch (const std::exception& error)
        {
            return failure(error.what());
        }
    }

    void MaterialLibrary::complete()
    {
        for (auto& item : pending_data_)
        {
            auto& loaded = loaded_.at(item.first);
            loaded.root = std::move(item.second.first);
            loaded.instance = std::move(item.second.second);
        }
        pending_.clear();
        previous_.clear();
        pending_shader_family_.clear();
        pending_data_.clear();
        published_ = false;
    }

    void MaterialLibrary::discard()
    {
        if (published_)
        {
            try
            {
                if (!MaterialInterface::publish_configurations(previous_))
                {
                    TOY_LOG_ERROR("Could not restore previous Material configurations.");
                }
            }
            catch (const std::exception& error)
            {
                TOY_LOG_ERROR("Material rollback failed: {}", error.what());
            }
        }
        pending_.clear();
        previous_.clear();
        pending_shader_family_.clear();
        pending_data_.clear();
        published_ = false;
    }

    void MaterialLibrary::shutdown()
    {
        discard();
        // Every stable proxy is retired before destroying any node. Shared
        // Parent references therefore cannot reorder proxy ownership transfers.
        for (auto it = temporary_.rbegin(); it != temporary_.rend(); ++it)
        {
            (*it)->retire_proxy();
        }
        for (auto& item : loaded_)
        {
            item.second.runtime->retire_proxy();
        }
        if (default_material_)
        {
            const_cast<Material*>(default_material_.get())->retire_proxy();
        }
        temporary_.clear();
        loaded_.clear();
        textures_ = {};
        default_material_.reset();
        shader_diagnostic_ = {};
    }
} // namespace toy3d
