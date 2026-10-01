#include "asset/material/material_asset.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void check(bool value, const char* message)
    {
        if (!value) { std::cerr << message << '\n'; std::exit(1); }
    }
}

int main()
{
    using namespace toy3d;
    TypeRegistry types;
    check(register_material_asset_types(types).succeeded() && types.freeze().succeeded(), "Material reflection registration failed");
    check(types.find("toy3d.MaterialAssetData") && types.find("toy3d.MaterialInstanceAssetData"), "Material root types missing");
    AssetId root_id;
    AssetId child_id;
    AssetId texture_id;
    check(AssetId::parse("11111111111111111111111111111111", root_id) &&
        AssetId::parse("22222222222222222222222222222222", child_id) &&
        AssetId::parse("33333333333333333333333333333333", texture_id), "Fixture IDs failed");
    AssetRef texture;
    texture.asset_id = texture_id;
    texture.expected_type = "toy3d.Texture2DAssetData";
    MaterialAssetData root;
    root.shader_name = "Toy3d/Surface/Phong";
    root.two_sided = true;
    root.overrides = {{"scalar", 0.5f}, {"uv", Vector2(2, 3)}, {"normal", Vector3(0, 1, 0)},
        {"color", Vector4(1, 0.5f, 0, 1)}, {"texture", texture}, {"sampler", MaterialSamplerPreset::TrilinearWrap}};
    const auto bytes = encode_material_asset(types, root_id, root);
    check(bytes.succeeded(), "Typed material asset did not encode");
    auto reordered = root;
    std::reverse(reordered.overrides.begin(), reordered.overrides.end());
    const auto sorted_bytes = encode_material_asset(types, root_id, reordered);
    check(sorted_bytes.succeeded() && sorted_bytes.value() == bytes.value(), "Override order changed deterministic bytes");
    const auto inspected = inspect_asset_bytes(bytes.value());
    check(inspected.succeeded() && inspected.value().dependencies.size() == 1u &&
        inspected.value().dependencies[0].asset_id == texture_id, "Texture references missing from asset dependencies");
    const auto& segment = inspected.value().segments.front();
    const std::vector<std::uint8_t> typed(bytes.value().begin() + static_cast<std::ptrdiff_t>(segment.offset),
        bytes.value().begin() + static_cast<std::ptrdiff_t>(segment.offset + segment.length));
    ValueReader reader(typed);
    MaterialAssetData restored;
    check(decode_value(reader, restored).succeeded() && reader.at_end() && restored.two_sided &&
        restored.shader_name == root.shader_name && restored.overrides.size() == 6u, "Typed material roundtrip failed");
    // get_if verifies that the persisted variant retained its type and exact authored value.
    check(std::get_if<Vector4>(&restored.overrides.front().value) &&
        *std::get_if<Vector4>(&restored.overrides.front().value) == Vector4(1, 0.5f, 0, 1), "Color variant changed");
    auto bad = root;
    bad.overrides.push_back(bad.overrides.front());
    check(!encode_material_asset(types, root_id, bad).succeeded(), "Duplicate overrides accepted");
    bad = root;
    bad.overrides.front().value = std::numeric_limits<float>::infinity();
    check(!validate_material_asset(bad).succeeded(), "Non-finite material value accepted");
    bad = root;
    bad.overrides.front().name = "0invalid";
    check(!validate_material_asset(bad).succeeded(), "Invalid canonical name accepted");
    AssetIndex index;
    AssetFileIndex root_index;
    root_index.asset_id = root_id;
    root_index.root_type = "toy3d.MaterialAssetData";
    root_index.schema_version = 1u;
    const auto root_path = VirtualPath::parse("/Engine/Root.asset");
    check(root_path.succeeded() && index.add(root_path.value(), root_index).succeeded(), "Root fixture index failed");
    MaterialInstanceAssetData child;
    child.parent.asset_id = root_id;
    child.parent.expected_type = "toy3d.MaterialAssetData";
    check(validate_material_instance_asset(child, &index).succeeded(), "Engine root should be a valid instance parent");
    const auto child_bytes = encode_material_instance_asset(types, child_id, child, &index);
    check(child_bytes.succeeded(), "Material instance creation failed");
    const auto child_index = inspect_asset_bytes(child_bytes.value());
    check(child_index.succeeded() && child_index.value().dependencies.size() == 1u &&
        child_index.value().dependencies[0].strength == AssetRefStrength::Strong, "Parent dependency lost");
    auto invalid_child = child;
    invalid_child.parent.expected_type = "toy3d.MaterialInstanceAssetData";
    check(!validate_material_instance_asset(invalid_child, &index).succeeded(), "Actual root type mismatch accepted");
    const auto child_path = VirtualPath::parse("/Project/Child.asset");
    check(child_path.succeeded() && index.add(child_path.value(), child_index.value()).succeeded(), "Child index failed");
    MaterialInstanceAssetData nested;
    nested.parent.asset_id = child_id;
    nested.parent.expected_type = "toy3d.MaterialInstanceAssetData";
    check(validate_material_instance_asset(nested, &index).succeeded(), "Instance Parent must accept its actual DTO type");
    check(nested.overrides.empty(), "New instance must not copy inherited overrides");
    invalid_child = child;
    invalid_child.parent.strength = AssetRefStrength::Weak;
    check(!validate_material_instance_asset(invalid_child, &index).succeeded(), "Weak parent accepted");
    invalid_child = child;
    invalid_child.parent.asset_id = texture_id;
    check(!validate_material_instance_asset(invalid_child, &index).succeeded(), "Missing parent accepted");
    check(!validate_material_asset(root, &index).succeeded(), "Whole-array missing texture reference escaped validation");
    root.overrides.clear();
    root.overrides.push_back({"orphan_parameter", 3.0f});
    check(encode_material_asset(types, root_id, root).succeeded(), "Known orphan value should remain persistable");
    std::cout << "Material asset tests passed\n";
    return 0;
}
