#include "asset_tools/material_asset_tools.h"

#include <algorithm>

#include "text/utf8.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        AssetStatus fail(const std::string& message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }

        AssetStatus prepare(EditorWorkspace& workspace, const std::string& destination, AssetId& id)
        {
            const auto parsed = VirtualPath::parse(destination);
            if (!workspace.ready() || !parsed.succeeded() || parsed.value().utf8() != destination ||
                destination.compare(0, 9, "/Project/") != 0 || destination.size() < 6u ||
                destination.substr(destination.size() - 6u) != ".asset")
                return fail("Choose an asset destination inside a writable Project directory.");
            const auto existing = workspace.files().stat(parsed.value());
            if (existing.succeeded()) return fail("An asset already exists at this path. Choose another name.");
            if (existing.status().code != FileErrorCode::NotFound) return fail(existing.status().message);
            const std::string folder = destination.substr(0, destination.find_last_of('/'));
            const auto parent = VirtualPath::parse(folder);
            if (!parent.succeeded()) return fail(parent.status().message);
            const auto directory = workspace.files().stat(parent.value());
            if (!directory.succeeded() || directory.value().type != FileType::Directory)
                return fail("Choose an existing Project directory.");
            if (!AssetId::try_generate(id) || workspace.catalog().index.find(id))
                return fail("Could not allocate a unique Asset ID; retry creation.");
            return AssetStatus::success();
        }

        AssetStatus publish(EditorWorkspace& workspace, const std::string& destination, const AssetId& id,
            const AssetResult<std::vector<std::uint8_t>>& bytes, AssetId& published_id)
        {
            if (!bytes.succeeded()) return bytes.status();
            const auto path = VirtualPath::parse(destination);
            if (!path.succeeded()) return fail(path.status().message);
            const FileStatus written = workspace.files().write_binary_atomic(path.value(), bytes.value(), FilePublishMode::CreateNew);
            if (!written.succeeded()) return {AssetErrorCode::Io, {}, destination, {}, {}, written.message, written};
            published_id = id;
            if (!workspace.refresh()) return {AssetErrorCode::InvalidState, id, destination, {}, {},
                "Asset was saved, but catalog refresh failed: " + workspace.error(), {}};
            return AssetStatus::success();
        }

        AssetStatus check_supported_schema(const shader::ShaderParameterSchema& schema)
        {
            std::string error;
            if (!shader::validate_shader_parameter_schema(schema, error)) return fail(error);
            for (const auto& buffer : schema.constant_buffers)
            {
                if (buffer.group != shader::BindingGroup::Material) continue;
                for (const auto& member : buffer.members)
                    if (member.array_count != 1u || member.matrix_stride != 0u ||
                        (member.type != shader::ShaderValueType::Float32 && member.type != shader::ShaderValueType::Float32x2 &&
                         member.type != shader::ShaderValueType::Float32x3 && member.type != shader::ShaderValueType::Float32x4))
                        return fail("This Shader has unsupported material constants.");
            }
            for (const auto& resource : schema.resources)
                if (resource.group == shader::BindingGroup::Material &&
                    (resource.category != shader::ShaderParameterCategory::SampledTexture ||
                     resource.resource_kind != shader::ResourceKind::Texture2D || resource.array_count != 1u ||
                     resource.default_value_kind != shader::ShaderParameterDefaultValueKind::String || resource.default_value != "white"))
                    return fail("This Shader requires resources that are not supported yet.");
            return AssetStatus::success();
        }
    }

    bool material_asset_destination(const std::string& folder, const std::string& name,
        std::string& destination, std::string& error)
    {
        if ((folder != "/Project" && folder.compare(0, 9, "/Project/") != 0) || !VirtualPath::parse(folder).succeeded())
        { error = "Choose a writable Project directory."; return false; }
        if (name.empty() || name.size() > 255u || !is_valid_utf8(name) || name == "." || name == ".." ||
            name.find_first_of("<>:\"/\\|?*") != std::string::npos || name.back() == '.' || name.back() == ' ' ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        { error = "Enter an asset name without extension, folders or reserved characters."; return false; }
        const std::string candidate = folder + "/" + name + ".asset";
        const auto parsed = VirtualPath::parse(candidate);
        if (!parsed.succeeded() || parsed.value().utf8() != candidate)
        { error = "Asset destination is invalid."; return false; }
        destination = candidate;
        error.clear();
        return true;
    }

    AssetStatus create_material_asset_in_workspace(EditorWorkspace& workspace, const std::string& destination,
        const MaterialAssetData& data, const shader::ShaderParameterSchema& schema, AssetId& published_id,
        const std::string& registered_shader_name)
    {
        if (data.shader_name != registered_shader_name) return fail("Choose a registered, compiled material Shader.");
        AssetStatus valid = check_supported_schema(schema);
        if (valid.succeeded()) valid = validate_material_overrides_schema(data.overrides, schema);
        if (!valid.succeeded()) return valid;
        AssetId id;
        valid = prepare(workspace, destination, id);
        if (!valid.succeeded()) return valid;
        return publish(workspace, destination, id,
            encode_material_asset(workspace.types(), id, data, &workspace.catalog().index), published_id);
    }

    AssetStatus create_material_instance_asset_in_workspace(EditorWorkspace& workspace, const std::string& destination,
        const MaterialInstanceAssetData& data, const shader::ShaderParameterSchema& schema, AssetId& published_id,
        const std::string& registered_shader_name)
    {
        AssetStatus valid = validate_material_instance_asset(data, &workspace.catalog().index);
        if (!valid.succeeded()) return valid;
        const auto hierarchy = read_material_hierarchy(workspace.types(), workspace.files(), workspace.catalog().index, data.parent);
        if (!hierarchy.succeeded()) return hierarchy.status();
        if (hierarchy.value().layers.size() >= maximum_material_parent_depth) return fail("Material Parent chain exceeds 64 layers.");
        if (hierarchy.value().root.shader_name != registered_shader_name) return fail("Parent Shader does not match its registered Program.");
        valid = check_supported_schema(schema);
        if (valid.succeeded()) valid = validate_material_overrides_schema(data.overrides, schema);
        if (!valid.succeeded()) return valid;
        AssetId id;
        valid = prepare(workspace, destination, id);
        if (!valid.succeeded()) return valid;
        return publish(workspace, destination, id,
            encode_material_instance_asset(workspace.types(), id, data, &workspace.catalog().index), published_id);
    }
}
