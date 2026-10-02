#include "assets/material/material_edit_session.h"

#include <algorithm>
#include <map>
#include <utility>

#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        void sort_overrides(std::vector<MaterialParameterOverride>& values)
        {
            std::sort(values.begin(), values.end(),
                      [](const MaterialParameterOverride& a, const MaterialParameterOverride& b)
                      {
                          return a.name < b.name;
                      });
        }

        bool same_references(const std::vector<AssetRef>& left, const std::vector<AssetRef>& right)
        {
            if (left.size() != right.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                if (!(left[i].asset_id == right[i].asset_id) || !(left[i].subresource_id == right[i].subresource_id) ||
                    left[i].strength != right[i].strength || left[i].expected_type != right[i].expected_type)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    AssetStatus MaterialEditSession::fail(AssetErrorCode code, const std::string& message) const
    {
        return {code, id_, path_.utf8(), {}, {}, message, {}};
    }

    AssetStatus MaterialEditSession::open(const AssetId& id, shader::ShaderParameterSchema schema,
                                          const std::string& registered_shader_name)
    {
        if (dirty() || gesturing())
        {
            return fail(AssetErrorCode::InvalidState, "Resolve unsaved changes before opening another material.");
        }
        const auto* location = workspace_.catalog().index.find(id);
        if (!location)
        {
            return fail(AssetErrorCode::MissingReference, "Material asset was not found.");
        }
        std::string error;
        if (!shader::validate_shader_parameter_schema(schema, error))
        {
            return fail(AssetErrorCode::Schema, error);
        }
        MaterialAssetData root;
        MaterialInstanceAssetData instance;
        MaterialAssetData parent;
        std::vector<MaterialAssetLayer> parent_layers;
        AssetStatus status;
        const bool child = location->index.root_type == "toy3d.MaterialInstanceAssetData";
        if (child)
        {
            status = read_material_instance_asset(workspace_.types(), workspace_.files(), location->path, instance,
                                                  &workspace_.catalog().index);
            if (!status.succeeded())
            {
                return status;
            }
            const auto hierarchy = read_material_hierarchy(workspace_.types(), workspace_.files(),
                                                           workspace_.catalog().index, instance.parent);
            if (!hierarchy.succeeded())
            {
                return hierarchy.status();
            }
            parent = hierarchy.value().root;
            parent.overrides = hierarchy.value().effective_overrides(schema);
            parent_layers = hierarchy.value().layers;
            status = AssetStatus::success();
        }
        else if (location->index.root_type == "toy3d.MaterialAssetData")
        {
            status = read_material_asset(workspace_.types(), workspace_.files(), location->path, root,
                                         &workspace_.catalog().index);
        }
        else
        {
            return fail(AssetErrorCode::TypeMismatch, "Open a Material or Material Instance asset.");
        }
        if (!status.succeeded())
        {
            return status;
        }
        if ((child ? parent.shader_name : root.shader_name) != registered_shader_name)
        {
            return fail(AssetErrorCode::Schema, "Material Shader does not match its registered Program.");
        }
        // Bind a complete candidate first. Failure leaves the previous session
        // intact; the callbacks observe this owner only after publication.
        std::unique_ptr<EditSession<MaterialAssetData>> next_root;
        std::unique_ptr<EditSession<MaterialInstanceAssetData>> next_instance;
        if (child)
        {
            const TypeDesc* type = workspace_.types().find(location->index.root_type);
            if (!type)
            {
                return fail(AssetErrorCode::Schema, "Material instance type is not registered.");
            }
            sort_overrides(instance.overrides);
            next_instance = std::make_unique<EditSession<MaterialInstanceAssetData>>(
                workspace_.types(), *type, id, location->path, instance,
                [this](const MaterialInstanceAssetData& data)
                {
                    return validate_instance(data);
                },
                &workspace_.catalog().index,
                [this](const MaterialInstanceAssetData& data, EditChangeKind)
                {
                    return prepare_instance(data);
                },
                [this](const EditRecord&)
                {
                    notify_instance();
                });
            status = next_instance->bind_published_pair(workspace_.files());
        }
        else
        {
            const TypeDesc* type = workspace_.types().find(location->index.root_type);
            if (!type)
            {
                return fail(AssetErrorCode::Schema, "Material type is not registered.");
            }
            sort_overrides(root.overrides);
            next_root = std::make_unique<EditSession<MaterialAssetData>>(
                workspace_.types(), *type, id, location->path, root,
                [this](const MaterialAssetData& data)
                {
                    return validate_material_asset(data, &workspace_.catalog().index);
                },
                &workspace_.catalog().index,
                [this](const MaterialAssetData& data, EditChangeKind)
                {
                    return prepare(data.overrides);
                },
                [this](const EditRecord&)
                {
                    notify(root_->value().overrides);
                });
            status = next_root->bind_published_pair(workspace_.files());
        }
        if (!status.succeeded())
        {
            return status;
        }
        clear();
        id_ = id;
        path_ = location->path;
        opened_index_ = location->index;
        schema_ = std::move(schema);
        parent_ = std::move(parent);
        parent_layers_ = std::move(parent_layers);
        root_ = std::move(next_root);
        instance_ = std::move(next_instance);
        return AssetStatus::success();
    }

    void MaterialEditSession::set_parent_preview(SchemaResolver schema,
                                                 std::function<AssetStatus(const MaterialAssetData&)> prepare_callback,
                                                 std::function<void()> notify_callback)
    {
        schema_resolver_ = std::move(schema);
        parent_prepare_ = std::move(prepare_callback);
        parent_notify_ = std::move(notify_callback);
    }

    AssetStatus MaterialEditSession::validate_instance(const MaterialInstanceAssetData& value) const
    {
        const auto valid = validate_material_instance_asset(value, &workspace_.catalog().index);
        if (!valid.succeeded())
        {
            return valid;
        }
        const auto hierarchy =
            read_material_hierarchy(workspace_.types(), workspace_.files(), workspace_.catalog().index, value.parent);
        if (!hierarchy.succeeded())
        {
            return hierarchy.status();
        }
        if (hierarchy.value().layers.size() >= maximum_material_parent_depth)
        {
            return fail(AssetErrorCode::Value, "Material Parent chain exceeds 64 layers.");
        }
        // Reuse the generic strong-dependency checker with this draft's edges;
        // the published catalog and files remain untouched.
        AssetIndex candidate;
        for (const auto& entry : workspace_.catalog().entries)
        {
            auto file = entry.file;
            if (file.asset_id == id_)
            {
                file.dependencies = material_asset_dependencies(value);
            }
            const auto added = candidate.add(entry.path, file);
            if (!added.succeeded())
            {
                return added;
            }
        }
        return candidate.validate_strong_dependencies();
    }

    AssetStatus MaterialEditSession::prepare_instance(const MaterialInstanceAssetData& value)
    {
        parent_prepared_ = false;
        const auto& current = instance_->value().parent;
        if (current.asset_id == value.parent.asset_id && current.expected_type == value.parent.expected_type)
        {
            return prepare(value.overrides);
        }
        const auto hierarchy =
            read_material_hierarchy(workspace_.types(), workspace_.files(), workspace_.catalog().index, value.parent);
        if (!hierarchy.succeeded())
        {
            return hierarchy.status();
        }
        auto schema = schema_;
        if (schema_resolver_)
        {
            const auto resolved = schema_resolver_(hierarchy.value().root.shader_name);
            if (!resolved.succeeded())
            {
                return resolved.status();
            }
            schema = resolved.value();
        }
        else if (hierarchy.value().root.shader_name != parent_.shader_name)
        {
            return fail(AssetErrorCode::Schema, "A validated Program is required to switch Parent Shader.");
        }
        MaterialAssetData effective = hierarchy.value().root;
        MaterialAssetHierarchy all = hierarchy.value();
        all.layers.push_back({{}, value.overrides});
        effective.overrides = all.effective_overrides(schema);
        if (parent_prepare_)
        {
            const auto status = parent_prepare_(effective);
            if (!status.succeeded())
            {
                return status;
            }
        }
        pending_parent_ = hierarchy.value().root;
        pending_parent_.overrides = hierarchy.value().effective_overrides(schema);
        pending_parent_layers_ = hierarchy.value().layers;
        pending_parent_schema_ = std::move(schema);
        parent_prepared_ = true;
        return AssetStatus::success();
    }

    void MaterialEditSession::notify_instance()
    {
        if (parent_prepared_)
        {
            parent_ = std::move(pending_parent_);
            parent_layers_ = std::move(pending_parent_layers_);
            schema_ = std::move(pending_parent_schema_);
            parent_prepared_ = false;
            if (parent_notify_)
            {
                parent_notify_();
            }
        }
        notify(instance_->value().overrides);
    }

    AssetStatus MaterialEditSession::set_parent(const AssetRef& parent)
    {
        if (!instance_ || !writable() || gesturing())
        {
            return fail(AssetErrorCode::InvalidState, "End the gesture before changing Parent.");
        }
        ValueWriter writer;
        const auto encoded = encode_value(writer, parent);
        if (!encoded.succeeded())
        {
            return fail(AssetErrorCode::Value, encoded.message);
        }
        PropertyPath path{PropertyPathPart::field("parent")};
        const auto edited = instance_->apply_edit({EditPatch{path, writer.bytes(), EditChangeKind::ReplaceCandidate}});
        return edited.succeeded() ? AssetStatus::success() : edited.status();
    }

    AssetRef MaterialEditSession::parameter_source(const std::string& name) const
    {
        for (const auto& value : overrides())
        {
            if (value.name == name && material_override_matches_schema(value, schema_))
            {
                return {id_,
                        {},
                        is_instance() ? "toy3d.MaterialInstanceAssetData" : "toy3d.MaterialAssetData",
                        AssetRefStrength::Strong};
            }
        }
        for (auto layer = parent_layers_.rbegin(); layer != parent_layers_.rend(); ++layer)
        {
            for (const auto& value : layer->overrides)
            {
                if (value.name == name && material_override_matches_schema(value, schema_))
                {
                    return layer->reference;
                }
            }
        }
        return {}; // Shader default.
    }

    AssetStatus MaterialEditSession::publish_saved()
    {
        if (!active())
        {
            return fail(AssetErrorCode::InvalidState, "No active Material asset.");
        }
        if (!publish_)
        {
            return AssetStatus::success();
        }
        AssetRef reference;
        reference.asset_id = id_;
        reference.expected_type = is_instance() ? "toy3d.MaterialInstanceAssetData" : "toy3d.MaterialAssetData";
        auto status = publish_(reference);
        if (!status.succeeded())
        {
            status.message =
                "Asset is saved; rendering still uses the previous version. Retry Publish: " + status.message;
        }
        return status;
    }

    void MaterialEditSession::set_preview(PreviewPrepare prepare_callback, PreviewNotify notify_callback)
    {
        preview_prepare_ = std::move(prepare_callback);
        preview_notify_ = std::move(notify_callback);
    }

    void MaterialEditSession::clear()
    {
        root_.reset();
        instance_.reset();
        parent_layers_.clear();
        pending_parent_layers_.clear();
        pending_parent_ = {};
        pending_parent_schema_ = {};
        parent_prepared_ = false;
        schema_resolver_ = {};
        parent_prepare_ = {};
        parent_notify_ = {};
        preview_prepare_ = {};
        preview_notify_ = {};
        draft_.clear();
        gesture_before_.clear();
        gesture_active_ = false;
        id_ = {};
        path_ = {};
        opened_index_ = {};
        schema_ = {};
        parent_ = {};
    }

    bool MaterialEditSession::dirty() const
    {
        return root_ ? root_->dirty() : instance_ && instance_->dirty();
    }
    std::size_t MaterialEditSession::undo_count() const
    {
        return root_ ? root_->undo_count() : instance_ ? instance_->undo_count() : 0u;
    }
    std::size_t MaterialEditSession::redo_count() const
    {
        return root_ ? root_->redo_count() : instance_ ? instance_->redo_count() : 0u;
    }
    const MaterialAssetData& MaterialEditSession::root_data() const
    {
        return root_ ? root_->value() : parent_;
    }
    const MaterialInstanceAssetData* MaterialEditSession::instance_data() const
    {
        return instance_ ? &instance_->value() : nullptr;
    }

    const std::vector<MaterialParameterOverride>& MaterialEditSession::overrides() const
    {
        if (gesture_active_)
        {
            return draft_;
        }
        if (root_)
        {
            return root_->value().overrides;
        }
        if (instance_)
        {
            return instance_->value().overrides;
        }
        return draft_;
    }

    std::vector<MaterialParameterOverride> MaterialEditSession::effective(
        const std::vector<MaterialParameterOverride>& values) const
    {
        std::map<std::string, MaterialParameterOverride> merged;
        if (instance_)
        {
            for (const auto& value : parent_.overrides)
            {
                if (material_override_matches_schema(value, schema_))
                {
                    merged[value.name] = value;
                }
            }
        }
        for (const auto& value : values)
        {
            if (material_override_matches_schema(value, schema_))
            {
                merged[value.name] = value;
            }
        }
        std::vector<MaterialParameterOverride> result;
        for (const auto& value : merged)
        {
            result.push_back(value.second);
        }
        return result;
    }

    std::vector<MaterialParameterOverride> MaterialEditSession::effective_overrides() const
    {
        return effective(overrides());
    }

    std::vector<MaterialParameterOverride> MaterialEditSession::effective_overrides(
        const shader::ShaderParameterSchema& schema) const
    {
        std::map<std::string, MaterialParameterOverride> merged;
        if (instance_)
        {
            for (const auto& value : parent_.overrides)
            {
                if (material_override_matches_schema(value, schema))
                {
                    merged[value.name] = value;
                }
            }
        }
        for (const auto& value : overrides())
        {
            if (material_override_matches_schema(value, schema))
            {
                merged[value.name] = value;
            }
        }
        std::vector<MaterialParameterOverride> values;
        for (const auto& item : merged)
        {
            values.push_back(item.second);
        }
        return values;
    }

    AssetStatus MaterialEditSession::update_schema(shader::ShaderParameterSchema schema)
    {
        if (!active() || gesturing())
        {
            return fail(AssetErrorCode::InvalidState, "End the parameter gesture before applying compiled code.");
        }
        std::string error;
        if (!shader::validate_shader_parameter_schema(schema, error))
        {
            return fail(AssetErrorCode::Schema, error);
        }
        // Keep persisted overrides and EditSession history. Orphans are computed
        // against this new schema and become editable again if their type returns.
        schema_ = std::move(schema);
        return AssetStatus::success();
    }

    AssetStatus MaterialEditSession::effective_bytes(const std::vector<MaterialParameterOverride>& values,
                                                     std::vector<std::uint8_t>& bytes) const
    {
        MaterialAssetData resolved = root_data();
        resolved.overrides = effective(values);
        for (const auto& buffer : schema_.constant_buffers)
        {
            for (const auto& member : buffer.members)
            {
                if (std::any_of(resolved.overrides.begin(), resolved.overrides.end(),
                                [&](const MaterialParameterOverride& value)
                                {
                                    return value.name == member.name;
                                }))
                {
                    continue;
                }
                ValueReader reader(member.default_value);
                MaterialParameterOverride value;
                value.name = member.name;
                ValueStatus decoded;
                if (member.type == shader::ShaderValueType::Float32)
                {
                    float number = 0.0f;
                    decoded = reader.read_float32(number);
                    value.value = number;
                }
                else if (member.type == shader::ShaderValueType::Float32x2)
                {
                    Vector2 vector;
                    decoded = decode_value(reader, vector);
                    value.value = vector;
                }
                else if (member.type == shader::ShaderValueType::Float32x3)
                {
                    Vector3 vector;
                    decoded = decode_value(reader, vector);
                    value.value = vector;
                }
                else if (member.type == shader::ShaderValueType::Float32x4)
                {
                    Vector4 vector;
                    decoded = decode_value(reader, vector);
                    value.value = vector;
                }
                else
                {
                    continue;
                }
                if (!decoded.succeeded() || !reader.at_end())
                {
                    return fail(AssetErrorCode::Schema, "Shader numeric default is invalid.");
                }
                resolved.overrides.push_back(std::move(value));
            }
        }
        sort_overrides(resolved.overrides);
        ValueWriter writer;
        const auto encoded = encode_value(writer, resolved);
        if (!encoded.succeeded())
        {
            return fail(AssetErrorCode::Value, encoded.message);
        }
        bytes = writer.bytes();
        return AssetStatus::success();
    }

    AssetStatus MaterialEditSession::validate_overrides(const std::vector<MaterialParameterOverride>& values) const
    {
        MaterialAssetData candidate = root_data();
        candidate.overrides = values;
        return validate_material_asset(candidate, &workspace_.catalog().index);
    }

    AssetStatus MaterialEditSession::prepare(const std::vector<MaterialParameterOverride>& values) const
    {
        const AssetStatus valid = validate_overrides(values);
        if (!valid.succeeded())
        {
            return valid;
        }
        return preview_prepare_ ? preview_prepare_(effective(values)) : AssetStatus::success();
    }

    void MaterialEditSession::notify(const std::vector<MaterialParameterOverride>& values) const
    {
        if (preview_notify_)
        {
            preview_notify_(effective(values));
        }
    }

    AssetStatus MaterialEditSession::begin_gesture()
    {
        if (!writable() || gesture_active_)
        {
            return fail(AssetErrorCode::InvalidState, "Material is read only or a gesture is already active.");
        }
        draft_ = overrides();
        gesture_before_ = draft_;
        gesture_active_ = true;
        return AssetStatus::success();
    }

    AssetStatus MaterialEditSession::set_parameter(const MaterialParameterOverride& value)
    {
        if (!writable())
        {
            return fail(AssetErrorCode::ReadOnly, "Engine materials are read only.");
        }
        if (!material_override_matches_schema(value, schema_))
        {
            return fail(AssetErrorCode::Value, "Parameter name or type is incompatible with the Shader.");
        }
        auto values = overrides();
        const auto found = std::find_if(values.begin(), values.end(),
                                        [&](const MaterialParameterOverride& item)
                                        {
                                            return item.name == value.name;
                                        });
        if (found == values.end())
        {
            values.push_back(value);
        }
        else
        {
            *found = value;
        }
        sort_overrides(values);
        if (!gesture_active_)
        {
            return commit(std::move(values));
        }
        const AssetStatus valid = prepare(values);
        if (!valid.succeeded())
        {
            return valid;
        }
        draft_ = std::move(values);
        notify(draft_);
        return AssetStatus::success();
    }

    AssetStatus MaterialEditSession::remove_parameter(const std::string& name)
    {
        if (!writable() || gesture_active_)
        {
            return fail(AssetErrorCode::InvalidState, "Finish the active gesture before resetting a parameter.");
        }
        auto values = overrides();
        values.erase(std::remove_if(values.begin(), values.end(),
                                    [&](const MaterialParameterOverride& item)
                                    {
                                        return item.name == name;
                                    }),
                     values.end());
        return commit(std::move(values));
    }

    AssetStatus MaterialEditSession::commit(std::vector<MaterialParameterOverride> values)
    {
        sort_overrides(values);
        ValueWriter writer;
        MaterialAssetData material = root_data();
        MaterialInstanceAssetData instance = instance_ ? instance_->value() : MaterialInstanceAssetData{};
        material.overrides = values;
        instance.overrides = std::move(values);
        const ValueStatus encoded = root_ ? encode_value(writer, material) : encode_value(writer, instance);
        if (!encoded.succeeded())
        {
            return fail(AssetErrorCode::Value, encoded.message);
        }
        const TypeDesc* type = workspace_.types().find(opened_index_.root_type);
        if (!type)
        {
            return fail(AssetErrorCode::Schema, "Material type is not registered.");
        }
        const PropertyPath property{PropertyPathPart::field("overrides")};
        // Extract the generated array payload through the existing property
        // API; the editor never duplicates the container codec framing.
        const auto accessed = access_property(workspace_.types(), *type, writer.bytes(), property);
        if (!accessed.succeeded())
        {
            return accessed.status();
        }
        const EditPatch patch{property, accessed.value().value_bytes, EditChangeKind::Setter};
        const auto edited = root_ ? root_->apply_edit({patch}) : instance_->apply_edit({patch});
        return edited.succeeded() ? AssetStatus::success() : edited.status();
    }

    AssetStatus MaterialEditSession::finish_gesture()
    {
        if (!gesture_active_)
        {
            return AssetStatus::success();
        }
        std::vector<std::uint8_t> before;
        std::vector<std::uint8_t> after;
        AssetStatus compared = effective_bytes(gesture_before_, before);
        if (compared.succeeded())
        {
            compared = effective_bytes(draft_, after);
        }
        if (!compared.succeeded())
        {
            return compared;
        }
        if (before == after)
        {
            return cancel_gesture();
        }
        auto values = std::move(draft_);
        gesture_active_ = false;
        const AssetStatus status = commit(std::move(values));
        if (!status.succeeded())
        {
            notify(overrides());
        }
        draft_.clear();
        gesture_before_.clear();
        return status;
    }

    AssetStatus MaterialEditSession::cancel_gesture()
    {
        if (!gesture_active_)
        {
            return AssetStatus::success();
        }
        gesture_active_ = false;
        draft_.clear();
        gesture_before_.clear();
        const AssetStatus valid = prepare(overrides());
        if (valid.succeeded())
        {
            notify(overrides());
        }
        return valid;
    }

    AssetStatus MaterialEditSession::undo()
    {
        if (!writable() || gesturing())
        {
            return fail(AssetErrorCode::InvalidState, "Finish the gesture before undo.");
        }
        return root_ ? root_->undo() : instance_->undo();
    }
    AssetStatus MaterialEditSession::redo()
    {
        if (!writable() || gesturing())
        {
            return fail(AssetErrorCode::InvalidState, "Finish the gesture before redo.");
        }
        return root_ ? root_->redo() : instance_->redo();
    }

    AssetStatus MaterialEditSession::save()
    {
        if (!writable())
        {
            return fail(AssetErrorCode::ReadOnly, "Engine materials are read only.");
        }
        AssetStatus status = finish_gesture();
        if (!status.succeeded())
        {
            return status;
        }
        {
            const auto published = workspace_.asset_pairs().read(path_);
            if (!published.succeeded())
            {
                return published.status();
            }
            const auto& description = published.value().description;
            if (!(description.index.asset_id == opened_index_.asset_id) ||
                description.index.root_type != opened_index_.root_type ||
                description.index.schema_version != opened_index_.schema_version ||
                !same_references(description.index.dependencies, opened_index_.dependencies) ||
                description.index.subresources.size() != opened_index_.subresources.size() || description.has_meta ||
                description.type_data != (root_ ? root_->published_type_bytes() : instance_->published_type_bytes()))
            {
                return fail(AssetErrorCode::Conflict, "Material description changed since opening.");
            }
            for (std::size_t i = 0; i < description.index.subresources.size(); ++i)
            {
                if (!(description.index.subresources[i].id == opened_index_.subresources[i].id) ||
                    description.index.subresources[i].type_name != opened_index_.subresources[i].type_name)
                {
                    return fail(AssetErrorCode::Conflict, "Material subresources changed since opening.");
                }
            }
            AssetFileIndex next = description.index;
            next.dependencies =
                root_ ? material_asset_dependencies(root_->value()) : material_asset_dependencies(instance_->value());
            ValueWriter writer;
            const ValueStatus encoded =
                root_ ? encode_value(writer, root_->value()) : encode_value(writer, instance_->value());
            if (!encoded.succeeded())
            {
                return fail(AssetErrorCode::Value, encoded.message);
            }
            const auto candidate = encode_asset_pair(workspace_.types(), next, writer.bytes(), {});
            if (!candidate.succeeded())
            {
                return candidate.status();
            }
            status = workspace_.asset_pairs().publish(path_, candidate.value(), FilePublishMode::Replace);
            if (!status.succeeded())
            {
                return status;
            }
            status = root_ ? root_->mark_pair_saved(writer.bytes()) : instance_->mark_pair_saved(writer.bytes());
            if (!status.succeeded())
            {
                return status;
            }
            opened_index_ = next;
            if (!workspace_.refresh())
            {
                return fail(AssetErrorCode::InvalidState,
                            "Material was saved, but catalog refresh failed: " + workspace_.error());
            }
            return publish_saved();
        }
    }
} // namespace toy3d
