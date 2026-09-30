#include "asset_file.h"
#include "asset_index.h"
#include "asset_catalog.h"
#include "asset_meta.h"

#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }
}

int main()
{
    using namespace toy3d;
    AssetId id;
    check(AssetId::parse("00112233445566778899aabbccddeeff", id) && id.valid() &&
              id.hex() == "00112233445566778899aabbccddeeff", "asset ID roundtrip failed");
    AssetId rejected = id;
    check(!AssetId::parse("00112233445566778899AABBCCDDEEFF", rejected) && rejected == id,
          "noncanonical ID changed caller value");
    AssetRef reference;
    reference.asset_id = id;
    reference.expected_type = "toy3d.ModelAssetData";
    reference.strength = AssetRefStrength::Deferred;
    ValueWriter reference_writer;
    check(encode_value(reference_writer, reference).succeeded(), "asset reference encode failed");
    AssetRef restored_reference;
    ValueReader reference_reader(reference_writer.bytes());
    check(decode_value(reference_reader, restored_reference).succeeded() && reference_reader.at_end() &&
              restored_reference.asset_id == id && restored_reference.expected_type == reference.expected_type &&
              restored_reference.strength == AssetRefStrength::Deferred,
          "asset reference roundtrip failed");
    AssetId second_id;
    SubresourceId mesh_id;
    check(AssetId::parse("102132435465768798a9babbdcddedef", second_id) &&
              SubresourceId::parse("ffeeddccbbaa99887766554433221100", mesh_id),
          "index IDs failed to parse");
    AssetMetaFile meta;
    meta.asset_id = id;
    meta.segments = {{"texture_mips", 2u, true, {4u, 5u}},
                     {"render_geometry", 2u, true, {1u, 2u, 3u}}};
    const auto meta_bytes = encode_asset_meta(meta);
    check(meta_bytes.succeeded(), "meta encoding failed");
    const auto decoded_meta = decode_asset_meta(meta_bytes.value());
    check(decoded_meta.succeeded() && decoded_meta.value().asset_id == id &&
        decoded_meta.value().segments.size() == 2u &&
        decoded_meta.value().segments[0].name == "render_geometry" &&
        decoded_meta.value().segments[0].bytes == std::vector<std::uint8_t>({1u, 2u, 3u}),
        "meta roundtrip or deterministic ordering failed");
    std::vector<std::uint8_t> damaged_meta = meta_bytes.value();
    damaged_meta.pop_back();
    check(!decode_asset_meta(damaged_meta).succeeded(), "truncated meta was accepted");
    damaged_meta = meta_bytes.value();
    std::fill(damaged_meta.begin() + 16u, damaged_meta.begin() + 32u, 0u);
    check(!decode_asset_meta(damaged_meta).succeeded(), "invalid meta identity was accepted");
    AssetFileIndex first_index;
    first_index.asset_id = id;
    first_index.root_type = "toy3d.SceneAsset";
    AssetRef second_ref;
    second_ref.asset_id = second_id;
    second_ref.expected_type = "toy3d.ModelAsset";
    first_index.dependencies.push_back(second_ref);
    AssetFileIndex second_index;
    second_index.asset_id = second_id;
    second_index.root_type = "toy3d.ModelAsset";
    second_index.subresources.push_back({mesh_id, "toy3d.MeshAsset"});
    auto first_path = VirtualPath::parse("/asset/scenes/first.asset");
    auto second_path = VirtualPath::parse("/asset/arbitrary/model.asset");
    auto moved_path = VirtualPath::parse("/asset/other/model.asset");
    check(first_path.succeeded() && second_path.succeeded() && moved_path.succeeded(),
          "index virtual paths failed");
    AssetIndex locations;
    check(locations.add(first_path.value(), first_index).succeeded() &&
              locations.add(second_path.value(), second_index).succeeded() &&
              locations.validate_strong_dependencies().succeeded(), "asset index setup failed");
    check(locations.add(moved_path.value(), second_index).code == AssetErrorCode::DuplicateIdentity,
          "duplicate asset ID was accepted");
    check(locations.move(second_id, moved_path.value()).succeeded() &&
              locations.find(second_id)->path == moved_path.value() &&
              locations.resolve(second_ref, "model").succeeded(),
          "moving an asset broke an ID reference");
    AssetRef mesh_ref = second_ref;
    mesh_ref.subresource_id = mesh_id;
    mesh_ref.expected_type = "toy3d.MeshAsset";
    check(locations.resolve(mesh_ref, "mesh_override").succeeded(), "subresource reference failed");
    mesh_ref.expected_type = "toy3d.MaterialAsset";
    check(locations.resolve(mesh_ref, "mesh_override").code == AssetErrorCode::TypeMismatch,
          "subresource type mismatch was accepted");
    SubresourceId absent_mesh;
    check(SubresourceId::parse("0123456789abcdef0123456789abcdef", absent_mesh), "missing subresource fixture failed");
    mesh_ref.subresource_id = absent_mesh;
    const AssetStatus missing_mesh = locations.resolve(mesh_ref, "model.mesh_override");
    check(missing_mesh.code == AssetErrorCode::MissingReference &&
              missing_mesh.property_path == "model.mesh_override",
          "missing subresource did not report the reference location");
    AssetId missing_id;
    check(AssetId::parse("ffffffffffffffffffffffffffffffff", missing_id), "missing ID fixture failed");
    mesh_ref.asset_id = missing_id;
    const AssetStatus missing = locations.resolve(mesh_ref, "actor.mesh");
    check(missing.code == AssetErrorCode::MissingReference && missing.property_path == "actor.mesh",
          "missing reference lost its property path");
    AssetIndex cyclic;
    AssetRef back_ref;
    back_ref.asset_id = id;
    back_ref.expected_type = "toy3d.SceneAsset";
    second_index.dependencies.push_back(back_ref);
    check(cyclic.add(first_path.value(), first_index).succeeded() &&
              cyclic.add(second_path.value(), second_index).succeeded(), "cycle fixture setup failed");
    const AssetStatus cycle = cyclic.validate_strong_dependencies();
    check(cycle.code == AssetErrorCode::DependencyCycle &&
              cycle.message.find(id.hex()) != std::string::npos &&
              cycle.message.find(second_id.hex()) != std::string::npos,
          "strong dependency cycle was not reported with its path");
    AssetIndex deferred;
    second_index.dependencies[0].strength = AssetRefStrength::Deferred;
    check(deferred.add(first_path.value(), first_index).succeeded() &&
              deferred.add(second_path.value(), second_index).succeeded() &&
              deferred.validate_strong_dependencies().succeeded(),
          "explicit deferred dependency was treated as a strong cycle");
    const std::vector<ImportedSubresource> previous = {{"node_left", mesh_id}};
    auto matching = match_subresources(previous, {"new_node", "node_left"});
    check(matching.succeeded() && matching.value().matched.size() == 1 &&
              matching.value().matched[0].id == mesh_id &&
              matching.value().new_source_keys.size() == 1 && matching.value().orphaned.empty(),
          "source node reorder rebound a subresource ID");
    auto orphaned = match_subresources(previous, {"new_node"});
    check(orphaned.succeeded() && orphaned.value().orphaned.size() == 1 &&
              orphaned.value().orphaned[0].id == mesh_id,
          "missing source node silently reassigned an old subresource ID");
    AssetFileIndex index;
    index.asset_id = id;
    index.root_type = "toy3d.ModelAssetData";
    index.schema_version = 2;
    std::vector<AssetSegmentData> segments = {
        {"vertices", 2, false, std::vector<std::uint8_t>(1024 * 1024, 0xabu)},
        {"type_data", 1, true, {1u, 2u, 3u}}};
    auto encoded = encode_asset_file(index, segments);
    check(encoded.succeeded(), "asset file encode failed");
    check(encoded.value().size() > 1024 * 1024 && encoded.value()[0] == 'T' &&
              encoded.value()[7] == 'T' && encoded.value()[8] == 1u && encoded.value()[9] == 0u,
          "magic or little-endian version changed");
    std::reverse(segments.begin(), segments.end());
    auto reordered = encode_asset_file(index, segments);
    check(reordered.succeeded() && reordered.value() == encoded.value(),
          "input segment insertion order changed canonical bytes");

    // C++17 filesystem is limited to creating and removing this isolated fixture;
    // asset I/O itself goes through the shared FileSystem.
    namespace fs = std::filesystem;
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("toy3d_asset_file_" + std::to_string(stamp));
    check(fs::create_directory(root), "fixture directory create failed");
    NativePlatformFile platform;
    DirectoryFileStoreDesc store_desc;
    store_desc.physical_root = PhysicalPath(root.u8string());
    auto store = DirectoryFileStore::create(platform, store_desc);
    check(store.succeeded(), "fixture store create failed");
    FileSystem files;
    auto mount_root = VirtualPath::parse("/asset");
    auto path = VirtualPath::parse("/asset/model.asset");
    check(mount_root.succeeded() && path.succeeded(), "fixture virtual path failed");
    FileMountDesc mount;
    mount.virtual_root = mount_root.value();
    mount.store = store.value();
    mount.access = MountAccess::ReadWrite;
    mount.allow_enumeration = true;
    check(files.add_mount(mount).succeeded() && files.freeze().succeeded(), "fixture mount failed");
    check(files.write_binary(path.value(), encoded.value(), FileWriteMode::CreateNew).succeeded(),
          "fixture file write failed");
    auto summary = inspect_asset(files, path.value());
    check(summary.succeeded() && summary.value().asset_id == id &&
              summary.value().root_type == "toy3d.ModelAssetData" &&
              summary.value().segments.size() == 2 &&
              summary.value().segments[1].name == "vertices" &&
              summary.value().segments[1].length == 1024 * 1024,
          "index inspection failed");
    std::vector<std::uint8_t> overlap = encoded.value();
    const std::uint64_t second_offset = summary.value().segments[1].offset;
    const std::uint64_t first_offset = summary.value().segments[0].offset;
    bool patched_offset = false;
    const std::uint32_t index_size = static_cast<std::uint32_t>(overlap[12]) |
        (static_cast<std::uint32_t>(overlap[13]) << 8u) |
        (static_cast<std::uint32_t>(overlap[14]) << 16u) |
        (static_cast<std::uint32_t>(overlap[15]) << 24u);
    for (std::size_t position = 16; position + 8 <= 16u + index_size; ++position)
    {
        bool matches = true;
        for (std::size_t byte = 0; byte < 8; ++byte)
            matches = matches && overlap[position + byte] ==
                static_cast<std::uint8_t>((second_offset >> (byte * 8u)) & 0xffu);
        if (!matches) continue;
        for (std::size_t byte = 0; byte < 8; ++byte)
            overlap[position + byte] = static_cast<std::uint8_t>((first_offset >> (byte * 8u)) & 0xffu);
        patched_offset = true;
        break;
    }
    check(patched_offset && files.write_binary(path.value(), overlap, FileWriteMode::Truncate).succeeded() &&
              inspect_asset(files, path.value()).status().code == AssetErrorCode::InvalidFormat,
          "overlapping segments were accepted");
    std::vector<std::uint8_t> unknown_kind = encoded.value();
    const std::string segment_name = "vertices";
    auto name_at = std::search(unknown_kind.begin() + 16, unknown_kind.begin() + 16 + index_size,
                               segment_name.begin(), segment_name.end());
    check(name_at != unknown_kind.begin() + 16 + index_size, "segment name fixture missing");
    *(name_at + static_cast<std::ptrdiff_t>(segment_name.size())) = 99u;
    check(files.write_binary(path.value(), unknown_kind, FileWriteMode::Truncate).succeeded() &&
              inspect_asset(files, path.value()).succeeded(),
          "unknown optional segment should remain inspectable");
    const auto required_kind_position = name_at + static_cast<std::ptrdiff_t>(segment_name.size()) + 1;
    *required_kind_position = 1u;
    check(files.write_binary(path.value(), unknown_kind, FileWriteMode::Truncate).succeeded() &&
              inspect_asset(files, path.value()).status().code == AssetErrorCode::UnknownRequiredSegment,
          "unknown required segment was accepted");
    std::vector<std::uint8_t> bad_version = encoded.value();
    bad_version[8] = 2u;
    check(files.write_binary(path.value(), bad_version, FileWriteMode::Truncate).succeeded() &&
              inspect_asset(files, path.value()).status().code == AssetErrorCode::UnsupportedVersion,
          "unknown file format version was accepted");
    std::vector<std::uint8_t> truncated(encoded.value().begin(), encoded.value().begin() + 17);
    check(files.write_binary(path.value(), truncated, FileWriteMode::Truncate).succeeded() &&
              inspect_asset(files, path.value()).status().code == AssetErrorCode::InvalidFormat,
          "truncated asset index was accepted");
    check(files.write_binary(path.value(), encoded.value(), FileWriteMode::Truncate).succeeded(),
          "restore before move failed");
    auto destination_folder = VirtualPath::parse("/asset/user_chosen_folder");
    auto destination = VirtualPath::parse("/asset/model_renamed.asset");
    check(destination_folder.succeeded() && destination.succeeded() &&
              files.create_directories(destination_folder.value()).succeeded(),
          "arbitrary destination folder failed");
    AssetIndex physical_locations;
    check(physical_locations.add(path.value(), summary.value()).succeeded(), "index add before move failed");
    const FileStatus renamed = files.rename_no_replace(path.value(), destination.value());
    check(renamed.succeeded(), "physical file rename failed");
    check(physical_locations.move(id, destination.value()).succeeded(), "index move failed");
    AssetRef root_reference;
    root_reference.asset_id = id;
    root_reference.expected_type = "toy3d.ModelAssetData";
    check(physical_locations.resolve(root_reference).succeeded() &&
              inspect_asset(files, destination.value()).succeeded(),
          "reference failed after physical file move");
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    check(!cleanup_error, "fixture cleanup failed");
    return 0;
}
