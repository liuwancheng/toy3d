#include "animation/animation_sequence.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

#include <yaml-cpp/yaml.h>

#include "asset/asset_pair.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "rendercore/geometry/skeletal_mesh_deformation.h"

namespace
{
    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }

    void mount(toy3d::NativePlatformFile& platform, toy3d::FileSystem& files, const char* physical, const char* root)
    {
        toy3d::DirectoryFileStoreDesc desc;
        desc.physical_root = toy3d::PhysicalPath(physical);
        const auto store = toy3d::DirectoryFileStore::create(platform, desc);
        check(store.succeeded(), "Manny fixture directory");
        toy3d::FileMountDesc entry;
        entry.store = store.value();
        entry.virtual_root = toy3d::VirtualPath::parse(root).value();
        entry.access = toy3d::MountAccess::ReadOnly;
        check(files.add_mount(entry).succeeded(), "Manny fixture mount");
    }

    toy3d::AssetPair read_pair(const toy3d::TypeRegistry& types, const toy3d::FileSystem& files,
                               const std::string& name)
    {
        const auto pair =
            toy3d::read_asset_pair(types, files, toy3d::VirtualPath::parse("/Assets/" + name + ".asset").value());
        check(pair.succeeded(), name + ": " + pair.status().message);
        return pair.value();
    }

    YAML::Node read_source(const toy3d::FileSystem& files, const std::string& name)
    {
        const auto text = files.read_text_utf8(toy3d::VirtualPath::parse("/Source/" + name).value());
        check(text.succeeded() && text.value().size() < 1024 * 1024, "bounded UE pose source");
        // JSON is a YAML subset; reuse the repository parser for controlled test data only.
        return YAML::Load(text.value());
    }

    toy3d::Matrix4 ue_local_matrix(const YAML::Node& node)
    {
        toy3d::Transform local;
        const auto position = node["translation"];
        const auto rotation = node["rotation_xyzw"];
        const auto scale = node["scale"];
        // The UE double snapshots contain subnormal values below float range. Parse as double
        // before narrowing; stream-based float parsing rejects these harmless near-zero values.
        local.translation =
            toy3d::Vector3(static_cast<float>(position[0].as<double>()), static_cast<float>(position[1].as<double>()),
                           static_cast<float>(position[2].as<double>()));
        local.rotation = toy3d::Quaternion(
            static_cast<float>(rotation[0].as<double>()), static_cast<float>(rotation[1].as<double>()),
            static_cast<float>(rotation[2].as<double>()), static_cast<float>(rotation[3].as<double>()));
        local.scale =
            toy3d::Vector3(static_cast<float>(scale[0].as<double>()), static_cast<float>(scale[1].as<double>()),
                           static_cast<float>(scale[2].as<double>()));
        // UE's default FBX exporter negates Y and declares RH Z-up, front=-Y.
        // The importer maps metadata into Toy3d Y-up, yielding (UE X, UE Z, UE Y).
        const toy3d::Matrix4 conversion(toy3d::Vector4(1, 0, 0, 0), toy3d::Vector4(0, 0, 1, 0),
                                        toy3d::Vector4(0, 1, 0, 0), toy3d::Vector4(0, 0, 0, 1));
        return conversion * toy3d::to_matrix(local) * conversion;
    }

    void compare_local(const toy3d::Matrix4& actual, const toy3d::Matrix4& expected, const std::string& context)
    {
        // FBX Euler export and fixed 30 Hz resampling have bounded interpolation error.
        // Use centimeters for translation and a separate dimensionless linear tolerance.
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t row = 0; row < 3; ++row)
            {
                const float tolerance = column == 3 ? 0.02f : 0.002f;
                check(std::abs(actual.at(column, row) - expected.at(column, row)) <= tolerance,
                      context + " matrix[" + std::to_string(column) + "," + std::to_string(row) + "] actual=" +
                          std::to_string(actual.at(column, row)) + " UE=" + std::to_string(expected.at(column, row)));
            }
        }
    }
} // namespace

int run_tests()
{
    using namespace toy3d;
    std::cout << std::unitbuf;
    NativePlatformFile platform;
    FileSystem files;
    mount(platform, files, TOY3D_MANNY_ASSETS, "/Assets");
    mount(platform, files, TOY3D_MANNY_SOURCE, "/Source");
    check(files.freeze().succeeded(), "fixture filesystem");
    TypeRegistry types;
    check(register_animation_asset_types(types).succeeded() && types.freeze().succeeded(), "animation types");
    const auto skeleton_pair = read_pair(types, files, "SKM_Manny_Skeleton");
    const auto skeleton = decode_skeleton_asset_pair(skeleton_pair);
    check(skeleton.succeeded() && skeleton.value().bones.size() == 161, "full Manny skeleton has exactly 161 bones");
    const auto skeleton_id = skeleton_pair.description.index.asset_id;
    auto layout = std::make_shared<const AnimationBoneLayout>(skeleton_id, skeleton.value());
    check(layout->status().succeeded(), "immutable Manny layout");
    const auto reference = read_source(files, "SK_Mannequin.reference.json")["bones"];
    std::map<std::string, std::size_t> source_indices;
    for (std::size_t i = 0; i < reference.size(); ++i)
    {
        source_indices.emplace(reference[i]["name"].as<std::string>(), i);
    }
    for (const auto& bone : skeleton.value().bones)
    {
        const auto node = reference[source_indices.at(bone.name)];
        const auto parent = node["parent_index"].as<int>();
        const std::string expected_parent = parent < 0 ? "" : reference[parent]["name"].as<std::string>();
        const std::string actual_parent = bone.parent_index < 0 ? "" : skeleton.value().bones[bone.parent_index].name;
        check(actual_parent == expected_parent, bone.name + " UE parent");
        compare_local(to_matrix(bone.reference_local_transform), ue_local_matrix(node["reference_local"]),
                      bone.name + " reference");
    }
    const auto mesh = decode_skeletal_mesh_asset_pair(read_pair(types, files, "SKM_Manny"));
    check(mesh.succeeded() &&
              validate_skeletal_mesh_compatibility(mesh.value(), skeleton_id, skeleton.value()).succeeded(),
          "Manny bind identity");
    SkeletalMeshDeformer deformer;
    check(deformer.set_mesh(layout, mesh.value()).succeeded(), "Manny deformer");
    const char* clips[] = {"MM_Idle", "MM_Walk_Fwd",  "MM_Walk_InPlace", "MM_Run_Fwd",
                           "MM_Jump", "MM_Fall_Loop", "MM_Land",         "MM_T_Pose"};
    for (const auto* name : clips)
    {
        std::cout << "Checking " << name << '\n';
        const auto asset = decode_animation_sequence_asset_pair(read_pair(types, files, name));
        check(asset.succeeded(), std::string(name) + " payload");
        AnimationSequence sequence(layout, asset.value());
        check(sequence.status().succeeded(), std::string(name) + " shared Skeleton identity");
        const auto source = read_source(files, std::string(name) + ".samples.json");
        check(std::abs(sequence.duration() - source["duration_seconds"].as<double>()) < 0.00001,
              std::string(name) + " duration");
        const auto names = source["bone_names"];
        std::map<std::string, std::size_t> indices;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            indices.emplace(names[i].as<std::string>(), i);
        }
        for (const auto& snapshot : source["snapshots"])
        {
            const auto time = snapshot["time"].as<double>();
            std::cout << "  time=" << time << '\n';
            // UE stores duration as float; FBX ticks preserve the rational frame interval.
            // End snapshots may exceed that interval by a few nanoseconds after export.
            const auto pose = sequence.sample(std::min(time, sequence.duration()));
            check(pose.succeeded(), std::string(name) + " pose sample: " + pose.status().message);
            for (std::size_t i = 0; i < skeleton.value().bones.size(); ++i)
            {
                const auto& bone = skeleton.value().bones[i];
                compare_local(to_matrix(pose.value().local_transforms[i]),
                              ue_local_matrix(snapshot["local_transforms"][indices.at(bone.name)]),
                              std::string(name) + " time=" + std::to_string(time) + " bone=" + bone.name);
            }
            const auto component = build_component_space_pose(pose.value());
            check(component.succeeded(), "Manny component-space pose");
            const auto deformation = deformer.evaluate({1, pose.value(), component.value()});
            check(deformation.succeeded() && deformation.value().has_mesh_bounds, "Manny dynamic bounds");
            // Test-only CPU oracle skins sparse vertices with the exact quantized GPU weights.
            const auto& geometry = mesh.value().geometry;
            for (std::size_t section_index = 0; section_index < geometry.mesh.sections.size(); ++section_index)
            {
                const auto& section = geometry.mesh.sections[section_index];
                const auto& map = geometry.section_bone_maps[section_index];
                for (std::size_t index = section.first_index; index < section.first_index + section.index_count;
                     index += 997)
                {
                    const auto vertex = geometry.mesh.indices[index];
                    Vector3 position;
                    for (std::size_t j = 0; j < geometry.num_bone_influences; ++j)
                    {
                        const auto& weights = geometry.skin_weights[vertex];
                        position += transform_position(deformation.value().skin_matrices[map[weights.bone_indices[j]]],
                                                       geometry.mesh.vertices[vertex].position) *
                                    (weights.weights[j] / 255.0f);
                    }
                    const auto& bounds = deformation.value();
                    check(is_finite(position) && position.x >= bounds.bounds_minimum.x &&
                              position.y >= bounds.bounds_minimum.y && position.z >= bounds.bounds_minimum.z &&
                              position.x <= bounds.bounds_maximum.x && position.y <= bounds.bounds_maximum.y &&
                              position.z <= bounds.bounds_maximum.z,
                          "quantized Manny skin vertex outside dynamic bounds");
                }
            }
        }
        std::cout << name << ": UE source poses, identity and bounds passed\n";
    }
    const auto simple_pair = read_pair(types, files, "SKM_Manny_Simple_Skeleton");
    const auto simple = decode_skeleton_asset_pair(simple_pair);
    check(simple.succeeded() && simple.value().bones.size() == 89 &&
              !validate_skeleton_compatibility(skeleton.value(), simple.value()).succeeded(),
          "Simple Manny must remain an independent 89-bone layout");
    return 0;
}

int main()
{
    try
    {
        return run_tests();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Manny fixture exception: " << exception.what() << '\n';
        return 1;
    }
}
