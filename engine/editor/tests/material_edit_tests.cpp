#include "assets/material/material_edit_session.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <string>

#include "workspace/editor_workspace.h"

namespace
{
    int failures = 0;
    void check(bool ok, const char* message)
    {
        if (!ok)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    }

    toy3d::VirtualPath path(const char* text)
    {
        const auto result = toy3d::VirtualPath::parse(text);
        check(result.succeeded(), "fixture path");
        return result.succeeded() ? result.value() : toy3d::VirtualPath{};
    }

    toy3d::shader::ShaderParameterSchema make_schema()
    {
        using namespace toy3d;
        using namespace toy3d::shader;
        ShaderParameterSchema schema;
        ShaderParameterConstantBufferSchema buffer;
        buffer.name = "material";
        buffer.binding_id =
            make_shader_parameter_id(BindingGroup::Material, ShaderParameterCategory::Constant, buffer.name);
        buffer.size = 32u;
        ShaderParameterConstantMemberSchema scalar;
        scalar.parameter_id =
            make_shader_parameter_id(BindingGroup::Material, ShaderParameterCategory::Constant, "roughness");
        scalar.name = "roughness";
        scalar.size = 4u;
        ValueWriter scalar_writer;
        check(scalar_writer.write_float32(0.25f).succeeded(), "default scalar");
        scalar.default_value = scalar_writer.bytes();
        ShaderParameterConstantMemberSchema color;
        color.parameter_id =
            make_shader_parameter_id(BindingGroup::Material, ShaderParameterCategory::Constant, "color");
        color.name = "color";
        color.type = ShaderValueType::Float32x4;
        color.offset = 16u;
        color.size = 16u;
        ValueWriter color_writer;
        check(encode_value(color_writer, Vector4(1, 1, 1, 1)).succeeded(), "default color");
        color.default_value = color_writer.bytes();
        buffer.members = {scalar, color};
        std::vector<ReflectedConstantMember> reflected;
        for (const auto& member : buffer.members)
        {
            reflected.push_back({member.parameter_id, member.name, member.type, member.offset, member.size, 0u, 0u});
        }
        buffer.data_layout_hash =
            calculate_constant_buffer_data_layout_hash(buffer.group, buffer.binding_id, buffer.size, reflected);
        schema.constant_buffers.push_back(buffer);
        schema.logical_layout_hash = calculate_shader_parameter_logical_layout_hash(schema);
        schema.schema_identity = calculate_shader_parameter_schema_identity(schema);
        return schema;
    }

    float scalar(const std::vector<toy3d::MaterialParameterOverride>& values)
    {
        // C++17 get_if checks the persisted variant shape before asserting a value.
        for (const auto& value : values)
        {
            if (value.name == "roughness")
            {
                if (const auto* number = std::get_if<float>(&value.value))
                {
                    return *number;
                }
            }
        }
        return -1.0f;
    }
} // namespace

int main()
{
    using namespace toy3d;
    NativePlatformFile platform;
    const std::string fixture = std::string(TOY3D_MATERIAL_TEST_ROOT) + "/" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    EditorWorkspacePaths paths;
    paths.project_assets = PhysicalPath(fixture + "/project/asset");
    paths.engine_assets = PhysicalPath(fixture + "/engine/asset");
    paths.editor_resources = PhysicalPath(fixture + "/editor/resources");
    paths.deployment = PhysicalPath(fixture + "/bin");
    for (const auto& directory : {paths.project_assets, paths.engine_assets, paths.editor_resources, paths.deployment})
    {
        check(platform.create_directories(directory).succeeded(), "fixture directories");
    }
    TypeRegistry types;
    check(register_material_asset_types(types).succeeded() && types.freeze().succeeded(), "fixture types");
    AssetId root_id;
    AssetId child_id;
    AssetId engine_id;
    check(AssetId::try_generate(root_id) && AssetId::try_generate(child_id) && AssetId::try_generate(engine_id),
          "fixture IDs");
    MaterialAssetData root;
    root.shader_name = "Toy3d/Surface/Phong";
    root.overrides = {{"roughness", 0.5f}, {"old_parameter", Vector2(2, 3)}};
    const auto root_bytes = encode_material_asset_pair(types, root_id, root);
    const auto engine_bytes = encode_material_asset_pair(types, engine_id, root);
    check(root_bytes.succeeded() && engine_bytes.succeeded(), "fixture assets");
    if (!root_bytes.succeeded() || !engine_bytes.succeeded())
    {
        return 1;
    }
    check(platform
              .write_binary(PhysicalPath(paths.project_assets.utf8() + "/M_Root.asset"), root_bytes.value().asset,
                            FileWriteMode::CreateNew)
              .succeeded(),
          "root publication");
    check(platform
              .write_binary(PhysicalPath(paths.engine_assets.utf8() + "/M_Engine.asset"), engine_bytes.value().asset,
                            FileWriteMode::CreateNew)
              .succeeded(),
          "engine publication");
    EditorWorkspace workspace;
    check(workspace.initialize(paths), "workspace");
    MaterialInstanceAssetData child;
    child.parent.asset_id = root_id;
    child.parent.expected_type = "toy3d.MaterialAssetData";
    const auto child_bytes = encode_material_instance_asset_pair(types, child_id, child, &workspace.catalog().index);
    check(child_bytes.succeeded(), "child encoding");
    if (!child_bytes.succeeded())
    {
        return 1;
    }
    check(workspace.asset_pairs()
                  .publish(path("/Project/MI_Child.asset"), child_bytes.value(), FilePublishMode::CreateNew)
                  .succeeded() &&
              workspace.refresh(),
          "child publication");
    const auto schema = make_schema();
    MaterialEditSession session(workspace);
    check(session.open(child_id, schema).succeeded() && session.is_instance() && !session.dirty(), "child opening");
    check(scalar(session.effective_overrides()) == 0.5f && session.overrides().empty(),
          "parent inherited without copied overrides");
    std::vector<MaterialParameterOverride> preview;
    session.set_preview(
        [](const std::vector<MaterialParameterOverride>& values)
        {
            if (scalar(values) == 9.0f)
            {
                return AssetStatus{AssetErrorCode::Value, {}, {}, {}, {}, "preview rejected", {}};
            }
            return AssetStatus::success();
        },
        [&](const std::vector<MaterialParameterOverride>& values)
        {
            preview = values;
        });
    check(session.begin_gesture().succeeded(), "gesture activation");
    check(session.set_parameter({"roughness", 0.6f}).succeeded() &&
              session.set_parameter({"roughness", 0.8f}).succeeded(),
          "continuous draft preview");
    check(!session.dirty() && session.undo_count() == 0u && scalar(preview) == 0.8f, "draft has no committed history");
    check(!session.set_parameter({"roughness", 9.0f}).succeeded() && scalar(preview) == 0.8f &&
              scalar(session.overrides()) == 0.8f,
          "failed draft retains last valid state");
    check(session.finish_gesture().succeeded() && session.undo_count() == 1u && session.dirty(),
          "one gesture one undo");
    check(session.undo().succeeded() && session.overrides().empty() && scalar(preview) == 0.5f && !session.dirty(),
          "undo reads current snapshot and restores inheritance");
    check(session.redo().succeeded() && scalar(preview) == 0.8f, "redo preview");
    check(session.begin_gesture().succeeded() && session.set_parameter({"roughness", 0.7f}).succeeded() &&
              session.cancel_gesture().succeeded() && scalar(preview) == 0.8f && session.undo_count() == 1u,
          "Escape restores committed value");
    check(session.begin_gesture().succeeded() && session.finish_gesture().succeeded() && session.undo_count() == 1u,
          "unchanged gesture adds no history");
    check(!session.set_parameter({"roughness", std::numeric_limits<float>::infinity()}).succeeded() &&
              !session.set_parameter({"unknown", 1.0f}).succeeded() && session.undo_count() == 1u,
          "invalid numeric and unknown parameter rejected");
    check(session.remove_parameter("roughness").succeeded() && session.overrides().empty() && scalar(preview) == 0.5f,
          "instance reset removes own override instead of copying parent");
    const auto checkpoint = session.undo_count();
    check(session.begin_gesture().succeeded() && session.set_parameter({"roughness", 0.6f}).succeeded() &&
              session.set_parameter({"roughness", 0.5f}).succeeded() && session.finish_gesture().succeeded() &&
              session.overrides().empty() && session.undo_count() == checkpoint,
          "dragging back to inherited value creates no override or history");
    check(session.set_parameter({"color", Vector4(0, 0, 1, 1)}).succeeded() &&
              session.set_parameter({"roughness", 0.75f}).succeeded(),
          "compound authored parameters");
    check(session.save().succeeded() && !session.dirty(), "save clears dirty only after publication");
    const auto saved = workspace.asset_pairs().read(path("/Project/MI_Child.asset"));
    check(saved.succeeded() && saved.value().description.index.dependencies.size() == 1u,
          "save preserves parent dependency");
    MaterialInstanceAssetData reopened;
    check(
        read_material_instance_asset(types, workspace.files(), path("/Project/MI_Child.asset"), reopened).succeeded() &&
            reopened.overrides.size() == 2u && reopened.overrides.front().name == "color",
        "deterministic saved override order");
    check(session.undo().succeeded() && session.dirty() && session.redo().succeeded() && !session.dirty(),
          "saved checkpoint survives undo and redo");
    check(session.set_parameter({"roughness", 0.9f}).succeeded(), "dirty before conflict");
    reopened.overrides.back().value = 0.1f;
    const auto external = encode_material_instance_asset_pair(types, child_id, reopened);
    check(external.succeeded(), "external editor encoding");
    if (external.succeeded())
    {
        check(workspace.asset_pairs()
                  .publish(path("/Project/MI_Child.asset"), external.value(), FilePublishMode::Replace)
                  .succeeded(),
              "external conflict publication");
    }
    const auto conflicting = session.save();
    check(conflicting.code == AssetErrorCode::Conflict && session.dirty(), "conflict retains dirty session");
    check(!session.open(root_id, schema).succeeded() && session.id() == child_id, "dirty switch rejected without loss");
    session.clear();
    check(session.open(root_id, schema).succeeded() && session.overrides().size() == 2u,
          "known orphan opens and persists");
    check(session.remove_parameter("old_parameter").succeeded() && session.undo().succeeded() &&
              session.overrides().size() == 2u,
          "orphan removal is undoable");
    session.clear();
    check(session.open(engine_id, schema).succeeded() && !session.writable() &&
              !session.set_parameter({"roughness", 0.2f}).succeeded() && !session.save().succeeded(),
          "Engine read only");
    session.clear();
    AssetId yaml_id;
    check(AssetId::try_generate(yaml_id), "YAML material ID");
    MaterialAssetData yaml_material;
    yaml_material.shader_name = root.shader_name;
    yaml_material.overrides = {{"roughness", 0.4f}};
    const auto yaml_pair = encode_material_asset_pair(types, yaml_id, yaml_material);
    check(yaml_pair.succeeded() && !yaml_pair.value().has_meta &&
              workspace.asset_pairs()
                  .publish(path("/Project/M_Yaml.asset"), yaml_pair.value(), FilePublishMode::CreateNew)
                  .succeeded() &&
              workspace.refresh(),
          "pure YAML material publication");
    if (yaml_pair.succeeded())
    {
        check(session.open(yaml_id, schema).succeeded(), "YAML material editor opening");
        check(session.set_parameter({"roughness", 0.65f}).succeeded() && session.dirty() &&
                  session.save().succeeded() && !session.dirty(),
              "YAML material edit/save");
        MaterialAssetData yaml_reopened;
        const auto descriptor = workspace.asset_pairs().read(path("/Project/M_Yaml.asset"));
        check(descriptor.succeeded() && !descriptor.value().description.has_meta &&
                  read_material_asset(types, workspace.files(), path("/Project/M_Yaml.asset"), yaml_reopened)
                      .succeeded() &&
                  scalar(yaml_reopened.overrides) == 0.65f &&
                  !workspace.files().stat(path("/Project/M_Yaml.meta")).succeeded(),
              "edited YAML material remains descriptive only");
        session.clear();
    }
    // Fixture lives beneath the configured build root; no user assets are touched.
    std::cout << (failures ? "Material editor tests failed\n" : "Material editor tests passed\n");
    return failures ? 1 : 0;
}
