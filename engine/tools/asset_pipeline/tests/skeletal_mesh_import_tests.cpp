#include "asset_pipeline/skeletal_mesh_import.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include "asset/asset_pair_store.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"

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

    bool mount(toy3d::NativePlatformFile& platform, toy3d::FileSystem& files, const char* path, const char* root,
               bool writable)
    {
        toy3d::DirectoryFileStoreDesc desc;
        desc.physical_root = toy3d::PhysicalPath(path);
        desc.writable = writable;
        const auto store = toy3d::DirectoryFileStore::create(platform, desc);
        if (!store.succeeded())
        {
            return false;
        }
        toy3d::FileMountDesc mount;
        mount.store = store.value();
        mount.virtual_root = toy3d::VirtualPath::parse(root).value();
        mount.access = writable ? toy3d::MountAccess::ReadWrite : toy3d::MountAccess::ReadOnly;
        mount.allow_enumeration = true;
        return files.add_mount(mount).succeeded();
    }
} // namespace

int main()
{
    using namespace toy3d;
    NativePlatformFile platform;
    check(platform.create_directories(PhysicalPath(TOY3D_SKELETAL_IMPORT_OUTPUT)).succeeded(), "test output directory");
    FileSystem files;
    check(mount(platform, files, TOY3D_SKELETAL_IMPORT_SAMPLES, "/Source", false) &&
              mount(platform, files, TOY3D_SKELETAL_IMPORT_OUTPUT, "/Output", true) && files.freeze().succeeded(),
          "test filesystem");
    AssetId skeleton_id;
    AssetId mesh_id;
    AssetId clip_id;
    check(AssetId::parse("11111111111111111111111111111111", skeleton_id) &&
              AssetId::parse("22222222222222222222222222222222", mesh_id) &&
              AssetId::parse("33333333333333333333333333333333", clip_id),
          "test ids");
    SkeletalMeshImportOptions options;
    options.coordinates.source_unit_in_centimeters = 100;
    const auto gltf =
        import_skeletal_mesh(files, VirtualPath::parse("/Source/two_bones.gltf").value(), skeleton_id, options);
    if (!gltf.succeeded())
    {
        std::cerr << gltf.status().message << '\n';
    }
    check(gltf.succeeded(), "analytic glTF skin import");
    const auto glb =
        import_skeletal_mesh(files, VirtualPath::parse("/Source/two_bones.glb").value(), skeleton_id, options);
    check(glb.succeeded(), "analytic GLB skin import");
    const auto eight =
        import_skeletal_mesh(files, VirtualPath::parse("/Source/eight_influences.gltf").value(), skeleton_id, options);
    if (!eight.succeeded())
    {
        std::cerr << eight.status().message << '\n';
    }
    check(eight.succeeded() && eight.value().mesh.geometry.num_bone_influences == 8,
          "JOINTS_1 and WEIGHTS_1 must survive glTF import");
    check(validate_skeletal_mesh_compatibility(eight.value().mesh, skeleton_id, eight.value().skeleton).succeeded() &&
              eight.value().mesh.geometry.section_bone_maps.size() == 1 &&
              eight.value().mesh.geometry.section_bone_maps[0].size() == 8,
          "eight imported influences retain identity bind and one draw section");
    for (const auto& skin : eight.value().mesh.geometry.skin_weights)
    {
        std::uint32_t sum = 0;
        for (const auto weight : skin.weights)
        {
            check(weight != 0, "source second influence group must not be discarded");
            sum += weight;
        }
        check(sum == 255, "eight imported weights must quantize to exactly 255");
    }
    const auto& imported = gltf.value();
    check(imported.mesh.geometry.num_bone_influences == 4, "two-bone import retains compact four-slot storage");
    check(validate_skeletal_mesh_compatibility(imported.mesh, skeleton_id, imported.skeleton).succeeded(),
          "import bind identity");
    check(imported.mesh.geometry.mesh.vertices.size() == 3 && imported.animations.size() == 1, "import counts");
    bool found_x = false;
    bool found_y = false;
    for (const auto& vertex : imported.mesh.geometry.mesh.vertices)
    {
        found_x = found_x || std::abs(vertex.position.x - 100) < 0.001f;
        found_y = found_y || std::abs(vertex.position.y - 200) < 0.001f;
        if (std::abs(vertex.position.x - 100) < 0.001f)
        {
            check(vertex.color == std::array<std::uint8_t, 4>{0, 255, 64, 255}, "source vertex color conversion");
        }
    }
    check(found_x && found_y, "centimeter mesh conversion");
    check(imported.animations[0].sequence.data.duration == 1 && imported.animations[0].sequence.data.sample_count == 31,
          "resample interval");
    const auto& track = imported.animations[0].sequence.tracks.front();
    check(imported.skeleton.bones[track.bone_index].name == "Tip" &&
              std::abs(track.samples.back().translation.x - 100) < 0.001f &&
              std::abs(track.samples.back().translation.y - 100) < 0.001f,
          "centimeter animation conversion");
    check(validate_skeleton_compatibility(imported.skeleton, glb.value().skeleton).succeeded(),
          "glTF and GLB hierarchy");
    check(imported.skeleton.bones.size() == 2 && imported.skeleton.bones.front().name == "Root",
          "scene container must not become an extra skeleton bone");
    auto reordered = eight.value().skeleton;
    std::swap(reordered.bones[1], reordered.bones[7]);
    const auto shared = import_skeletal_mesh(files, VirtualPath::parse("/Source/eight_influences.gltf").value(),
                                             skeleton_id, reordered, options);
    check(shared.succeeded() && shared.value().skeleton.bones[1].name == "Bone7" &&
              shared.value().mesh.geometry.section_bone_maps[0][1] == 7 &&
              validate_skeletal_mesh_compatibility(shared.value().mesh, skeleton_id, reordered).succeeded(),
          "existing skeleton order remaps section bone maps and bind data");
    const auto shared_animation = import_skeletal_mesh(
        files, VirtualPath::parse("/Source/eight_bones_animation.gltf").value(), skeleton_id, reordered, options);
    check(shared_animation.succeeded() && shared_animation.value().animations.size() == 1 &&
              shared_animation.value().animations[0].sequence.tracks.size() == 1 &&
              shared_animation.value().animations[0].sequence.tracks[0].bone_index == 1 &&
              std::abs(shared_animation.value().animations[0].sequence.tracks[0].samples.back().translation.x - 100) <
                  0.001f &&
              validate_animation_compatibility(shared_animation.value().animations[0].sequence, skeleton_id, reordered)
                  .succeeded(),
          "non-identical existing bone order remaps animation tracks by name and preserves samples");
    auto incompatible = imported.skeleton;
    incompatible.bones[1].reference_local_transform.translation.x += 1;
    check(!import_skeletal_mesh(files, VirtualPath::parse("/Source/two_bones.gltf").value(), skeleton_id, incompatible,
                                options)
               .succeeded(),
          "existing skeleton must reject a different reference pose");
    auto corrupt_mesh = imported.mesh;
    corrupt_mesh.geometry.inverse_bind_matrices[0].at(3, 3) = 1.01f;
    const auto corrupt = validate_skeletal_mesh_compatibility(corrupt_mesh, skeleton_id, imported.skeleton);
    check(!corrupt.succeeded() && corrupt.message.find("must be affine") != std::string::npos,
          "geometry errors retain their actual cause; projective bind matrices remain invalid");
    const auto source_text = files.read_text_utf8(VirtualPath::parse("/Source/two_bones.gltf").value());
    check(source_text.succeeded(), "negative fixture source");
    auto write_fixture = [&](const char* name, const std::string& text)
    {
        const auto path = VirtualPath::parse(name).value();
        const auto mode = files.stat(path).succeeded() ? FilePublishMode::Replace : FilePublishMode::CreateNew;
        check(files.write_binary_atomic(path, {text.begin(), text.end()}, mode).succeeded(), "write hierarchy fixture");
        return path;
    };
    auto duplicate_scene = source_text.value();
    const auto mesh_name = duplicate_scene.find("\"name\": \"Mesh\"");
    check(mesh_name != std::string::npos, "fixture mesh node");
    duplicate_scene.replace(mesh_name, 14, "\"name\": \"SceneRoot\"");
    const auto duplicate = import_skeletal_mesh(files, write_fixture("/Output/duplicate_scene.gltf", duplicate_scene),
                                                skeleton_id, options);
    check(duplicate.succeeded() &&
              validate_skeleton_compatibility(imported.skeleton, duplicate.value().skeleton).succeeded(),
          "legal non-bone node names can repeat without changing skeleton identity");
    auto ambiguous_bone = source_text.value();
    ambiguous_bone.replace(mesh_name, 14, "\"name\": \"Tip\"");
    check(
        !import_skeletal_mesh(files, write_fixture("/Output/ambiguous_bone.gltf", ambiguous_bone), skeleton_id, options)
             .succeeded(),
        "ambiguous bone name must still be rejected");
    auto translated_container = source_text.value();
    const auto container_name = translated_container.find("\"name\": \"SceneRoot\"");
    check(container_name != std::string::npos, "fixture scene container");
    translated_container.insert(container_name, "\"translation\": [5, 0, 0], ");
    const auto translated = import_skeletal_mesh(
        files, write_fixture("/Output/translated_container.gltf", translated_container), skeleton_id, options);
    check(translated.succeeded() && translated.value().skeleton.bones.size() == 2 &&
              std::abs(translated.value().skeleton.bones[0].reference_local_transform.translation.x - 500) < 0.001f &&
              std::abs(translated.value().mesh.geometry.mesh.vertices.front().position.x - 500) < 0.001f &&
              validate_skeletal_mesh_compatibility(translated.value().mesh, skeleton_id, translated.value().skeleton)
                  .succeeded(),
          "container removal preserves cumulative root, geometry and inverse bind transforms");
    auto step_source = source_text.value();
    const auto interpolation = step_source.find("\"LINEAR\"");
    check(interpolation != std::string::npos, "fixture has explicit source interpolation");
    step_source.replace(interpolation, 8, "\"STEP\"");
    const auto step_path = VirtualPath::parse("/Output/step.gltf").value();
    const auto step_mode = files.stat(step_path).succeeded() ? FilePublishMode::Replace : FilePublishMode::CreateNew;
    check(files.write_binary_atomic(step_path, {step_source.begin(), step_source.end()}, step_mode).succeeded(),
          "write negative interpolation fixture");
    const auto step_import = import_skeletal_mesh(files, step_path, skeleton_id, options);
    check(!step_import.succeeded(), "STEP must be rejected rather than resampled as linear");
    TypeRegistry types;
    check(register_animation_asset_types(types).succeeded() && types.freeze().succeeded(), "schema registry");
    const auto skeleton = encode_skeleton_asset_pair(types, skeleton_id, imported.skeleton);
    const auto mesh = encode_skeletal_mesh_asset_pair(types, mesh_id, imported.mesh);
    const auto clip = encode_animation_sequence_asset_pair(types, clip_id, imported.animations[0].sequence);
    check(skeleton.succeeded() && mesh.succeeded() && clip.succeeded(), "imported candidate encode");
    AssetPairStore store(types, files);
    auto publish = [&](const char* name, const AssetPairBytes& bytes)
    {
        const auto path = VirtualPath::parse(name).value();
        const auto found = files.stat(path);
        const auto mode = found.succeeded() ? FilePublishMode::Replace : FilePublishMode::CreateNew;
        const auto status = store.publish(path, bytes, mode);
        if (!status.succeeded())
        {
            std::cerr << status.message << '\n';
        }
        return status.succeeded();
    };
    check(publish("/Output/skeleton.asset", skeleton.value()), "skeleton publish");
    check(publish("/Output/mesh.asset", mesh.value()), "mesh publish");
    check(publish("/Output/clip.asset", clip.value()), "clip publish");
    const auto loaded = store.read(VirtualPath::parse("/Output/mesh.asset").value());
    check(loaded.succeeded() && decode_skeletal_mesh_asset_pair(loaded.value()).succeeded(), "paired mesh load");
    std::cout << "Skeletal glTF/GLB import and asset pair tests passed\n";
    return 0;
}
