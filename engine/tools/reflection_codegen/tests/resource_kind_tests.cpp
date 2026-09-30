#include "generated/fixture_reflection.h"

#include "asset_file.h"
#include "asset_index.h"
#include "asset_yaml.h"
#include "asset_pair.h"
#include "asset_pair_store.h"
#include "edit_session.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "frontend/shader_parser.h"
#include "format/shader_format_types.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

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

    toy3d::AssetId id(const char* text)
    {
        toy3d::AssetId result;
        check(toy3d::AssetId::parse(text, result), "fixture Asset ID is invalid");
        return result;
    }

    toy3d::SubresourceId sub_id(const char* text)
    {
        toy3d::SubresourceId result;
        check(toy3d::SubresourceId::parse(text, result), "fixture subresource ID is invalid");
        return result;
    }

    toy3d::AssetRef reference(const toy3d::AssetId& target, const char* type)
    {
        toy3d::AssetRef result;
        result.asset_id = target;
        result.expected_type = type;
        return result;
    }

    template <typename T> std::vector<std::uint8_t> encoded(const T& value)
    {
        toy3d::ValueWriter writer;
        check(toy3d::encode_value(writer, value).succeeded(), "resource fixture encode failed");
        T restored{};
        toy3d::ValueReader reader(writer.bytes());
        check(toy3d::decode_value(reader, restored).succeeded() && reader.at_end(),
              "resource fixture decode failed");
        toy3d::ValueWriter repeated;
        check(toy3d::encode_value(repeated, restored).succeeded() && repeated.bytes() == writer.bytes(),
              "resource fixture did not round trip canonically");
        return writer.bytes();
    }

    // C++17 filesystem creates only the isolated fixture directory; production
    // resource access stays on the shared FileSystem API.
    namespace fs = std::filesystem;
    class InterruptingFileStore final : public toy3d::FileStore
    {
      public:
        explicit InterruptingFileStore(std::shared_ptr<toy3d::FileStore> base)
            : base_(std::move(base)) {}

        void interrupt_descriptor_publish() { interrupt_descriptor_ = true; }

        toy3d::FileStoreCapabilities capabilities() const override { return base_->capabilities(); }
        toy3d::FileResult<toy3d::FileStat> stat(const toy3d::StorePath& path) const override
        { return base_->stat(path); }
        toy3d::FileResult<std::unique_ptr<toy3d::FileHandle>> open(
            const toy3d::StorePath& path, toy3d::FileOpenMode mode) override
        { return base_->open(path, mode); }
        toy3d::FileResult<std::vector<toy3d::StoreDirectoryEntry>> enumerate(
            const toy3d::StorePath& path) const override
        { return base_->enumerate(path); }
        toy3d::FileStatus create_directories(const toy3d::StorePath& path) override
        { return base_->create_directories(path); }
        toy3d::FileStatus remove_file(const toy3d::StorePath& path) override
        { return base_->remove_file(path); }
        toy3d::FileStatus remove_empty_directory(const toy3d::StorePath& path) override
        { return base_->remove_empty_directory(path); }
        toy3d::FileStatus rename_no_replace(const toy3d::StorePath& source,
            const toy3d::StorePath& destination) override
        {
            const std::string& name = source.utf8();
            if (interrupt_descriptor_ && name.size() >= 10u &&
                name.compare(name.size() - 10u, 10u, ".asset.new") == 0)
            {
                interrupt_descriptor_ = false;
                throw std::runtime_error("simulated process interruption before descriptor commit");
            }
            return base_->rename_no_replace(source, destination);
        }
        toy3d::FileStatus replace(const toy3d::StorePath& source,
            const toy3d::StorePath& destination) override
        { return base_->replace(source, destination); }

      private:
        std::shared_ptr<toy3d::FileStore> base_;
        bool interrupt_descriptor_ = false;
    };

    struct TestFiles
    {
        fs::path root;
        toy3d::NativePlatformFile platform;
        std::shared_ptr<toy3d::DirectoryFileStore> store;
        std::shared_ptr<InterruptingFileStore> interrupting_store;
        toy3d::FileSystem files;

        TestFiles()
        {
            const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            root = fs::temp_directory_path() / ("toy3d_resource_kinds_" + std::to_string(stamp));
            check(fs::create_directory(root), "resource fixture root creation failed");
            toy3d::DirectoryFileStoreDesc desc;
            desc.physical_root = toy3d::PhysicalPath(root.u8string());
            auto created = toy3d::DirectoryFileStore::create(platform, desc);
            check(created.succeeded(), "resource fixture store failed");
            store = created.value();
            interrupting_store = std::make_shared<InterruptingFileStore>(store);
            auto mount_root = toy3d::VirtualPath::parse("/asset");
            check(mount_root.succeeded(), "resource mount path failed");
            toy3d::FileMountDesc mount;
            mount.virtual_root = mount_root.value();
            mount.store = interrupting_store;
            mount.access = toy3d::MountAccess::ReadWrite;
            mount.allow_enumeration = true;
            check(files.add_mount(mount).succeeded() && files.freeze().succeeded(),
                  "resource fixture mount failed");
        }

        ~TestFiles()
        {
            std::error_code error;
            fs::remove_all(root, error);
            if (error) std::cerr << "resource fixture cleanup: " << error.message() << '\n';
        }

        toy3d::VirtualPath path(const char* name)
        {
            auto parsed = toy3d::VirtualPath::parse(std::string("/asset/") + name);
            check(parsed.succeeded(), "resource fixture path failed");
            return parsed.value();
        }
    };

    toy3d::AssetStatus invalid(const std::string& path, const char* message)
    {
        return {toy3d::AssetErrorCode::Value, {}, {}, {}, path, message, {}};
    }

    void test_model(const toy3d::TypeRegistry& types, TestFiles& fixture)
    {
        using namespace toy3d;
        const AssetId model_id = id("00112233445566778899aabbccddeeff");
        const AssetId material_id = id("11112222333344445555666677778888");
        const AssetId skeleton_id = id("22223333444455556666777788889999");
        const SubresourceId mesh_id = sub_id("aaaabbbbccccddddeeeeffff00001111");
        AssetIndex references;
        AssetFileIndex material_index;
        material_index.asset_id = material_id;
        material_index.root_type = "toy3d.MaterialAssetData";
        AssetFileIndex skeleton_index;
        skeleton_index.asset_id = skeleton_id;
        skeleton_index.root_type = "toy3d.SkeletonAsset";
        check(references.add(fixture.path("materials/mat.asset"), material_index).succeeded() &&
              references.add(fixture.path("skeletons/rig.asset"), skeleton_index).succeeded(),
              "model reference index failed");
        ModelAssetData model;
        model.source_uri = "source/vehicle.gltf";
        model.import_settings.unit_scale = 0.01f;
        model.skeleton = reference(skeleton_id, "toy3d.SkeletonAsset");
        ModelNodeData node;
        node.id = "node-a";
        node.source_key = "vehicle/body";
        node.mesh_subresource = mesh_id.hex();
        node.material_override = reference(material_id, "toy3d.MaterialAssetData");
        model.nodes.push_back(node);
        model.geometry_segment = "geometry_vertices";
        const std::vector<std::uint8_t> type_bytes = encoded(model);
        AssetFileIndex file_index;
        file_index.asset_id = model_id;
        file_index.root_type = "toy3d.ModelAssetData";
        file_index.schema_version = 1;
        file_index.subresources.push_back({mesh_id, "toy3d.Mesh"});
        file_index.dependencies.push_back(model.skeleton);
        file_index.dependencies.push_back(node.material_override);
        AssetYamlDocument yaml_document;
        yaml_document.index = file_index;
        yaml_document.type_data = type_bytes;
        const auto yaml_bytes = encode_asset_yaml(types, yaml_document);
        check(yaml_bytes.succeeded() &&
            std::string(yaml_bytes.value().begin(), yaml_bytes.value().end()).find("source/vehicle.gltf") != std::string::npos,
            "model YAML description encode failed");
        const auto yaml_read = decode_asset_yaml(types, yaml_bytes.value());
        if (!yaml_read.succeeded()) std::cerr << yaml_read.status().message << '\n';
        ModelAssetData yaml_model;
        if (yaml_read.succeeded())
        {
            ValueReader yaml_reader(yaml_read.value().type_data);
            check(decode_value(yaml_reader, yaml_model).succeeded() && yaml_reader.at_end(),
                "model YAML typed data decode failed");
        }
        check(yaml_read.succeeded() && yaml_read.value().index.asset_id == model_id &&
            yaml_read.value().index.dependencies.size() == 2u &&
            yaml_model.source_uri == model.source_uri && yaml_model.nodes.size() == model.nodes.size(),
            "model YAML description roundtrip failed");
        auto duplicate_yaml = yaml_bytes.value();
        const std::string repeated = "format_version: 2\n";
        duplicate_yaml.insert(duplicate_yaml.end(), repeated.begin(), repeated.end());
        check(!decode_asset_yaml(types, duplicate_yaml).succeeded(),
            "duplicate YAML key was accepted");
        const VirtualPath pure_path = fixture.path("pure_yaml.asset");
        const VirtualPath pure_meta = fixture.path("pure_yaml.meta");
        check(fixture.files.write_binary(pure_path, yaml_bytes.value(),
            FileWriteMode::CreateNew).succeeded() &&
            read_asset_pair(types, fixture.files, pure_path).succeeded() &&
            fixture.files.write_binary(pure_meta, {1u, 2u},
                FileWriteMode::CreateNew).succeeded() &&
            !read_asset_pair(types, fixture.files, pure_path).succeeded(),
            "orphan meta beside a descriptive asset was accepted");
        const std::vector<std::uint8_t> vertices(1024 * 1024, 0x77u);
        const auto pair = encode_asset_pair(types, file_index, type_bytes,
            {{model.geometry_segment, 2u, true, vertices}});
        check(pair.succeeded() && pair.value().has_meta, "model asset pair encode failed");
        const VirtualPath yaml_path = fixture.path("vehicle_yaml.asset");
        const VirtualPath meta_path = fixture.path("vehicle_yaml.meta");
        check(fixture.files.write_binary(meta_path, pair.value().meta, FileWriteMode::CreateNew).succeeded() &&
            fixture.files.write_binary(yaml_path, pair.value().asset, FileWriteMode::CreateNew).succeeded(),
            "model asset pair fixture write failed");
        const auto read_pair = read_asset_pair(types, fixture.files, yaml_path);
        check(read_pair.succeeded() && read_pair.value().meta.asset_id == model_id &&
            read_pair.value().meta.segments.size() == 1u &&
            read_pair.value().meta.segments[0].bytes == vertices,
            "model asset pair roundtrip failed");
        auto tampered = pair.value().meta;
        tampered.back() ^= 1u;
        check(fixture.files.write_binary_atomic(meta_path, tampered,
            FilePublishMode::Replace).succeeded() &&
            !read_asset_pair(types, fixture.files, yaml_path).succeeded() &&
            fixture.files.write_binary_atomic(meta_path, pair.value().meta,
                FilePublishMode::Replace).succeeded(),
            "meta SHA-256 mismatch was accepted");
        check(fixture.files.remove_file(meta_path).succeeded() &&
            !read_asset_pair(types, fixture.files, yaml_path).succeeded(),
            "missing required meta was accepted");
        AssetPairStore pair_store(types, fixture.files);
        const VirtualPath published_path = fixture.path("vehicle_published.asset");
        check(pair_store.publish(published_path, pair.value(), FilePublishMode::CreateNew).succeeded(),
            "pair CreateNew publication failed");
        check(!pair_store.publish(published_path, pair.value(), FilePublishMode::CreateNew).succeeded(),
            "pair CreateNew overwrote an existing asset");
        const auto pair_root = VirtualPath::parse("/asset");
        check(pair_root.succeeded(), "pair recovery root failed");
        const AssetStatus replaced_pair = pair_store.publish(published_path, pair.value(), FilePublishMode::Replace);
        const AssetStatus recovered_pair = pair_store.recover_tree(pair_root.value());
        const auto published_pair = read_asset_pair(types, fixture.files, published_path);
        if (!replaced_pair.succeeded()) std::cerr << "replace: " << replaced_pair.message << '\n';
        if (!recovered_pair.succeeded()) std::cerr << "recovery: " << recovered_pair.message << '\n';
        if (!published_pair.succeeded()) std::cerr << "read: " << published_pair.status().message << '\n';
        check(replaced_pair.succeeded() && recovered_pair.succeeded() && published_pair.succeeded(),
            "pair replacement or recovery failed");
        const std::vector<std::uint8_t> changed_vertices(vertices.size(), 0x33u);
        const auto interrupted_pair = encode_asset_pair(types, file_index, type_bytes,
            {{model.geometry_segment, 2u, true, changed_vertices}});
        check(interrupted_pair.succeeded(), "interrupted pair fixture encode failed");
        fixture.interrupting_store->interrupt_descriptor_publish();
        bool interrupted = false;
        try
        {
            (void)pair_store.publish(published_path, interrupted_pair.value(), FilePublishMode::Replace);
        }
        catch (const std::runtime_error&)
        {
            interrupted = true;
        }
        check(interrupted && pair_store.recover_tree(pair_root.value()).succeeded(),
            "interrupted pair update did not recover");
        const auto restored_pair = read_asset_pair(types, fixture.files, published_path);
        check(restored_pair.succeeded() && restored_pair.value().meta.segments[0].bytes == vertices &&
            !fixture.files.stat(fixture.path("vehicle_published.asset.txn")).succeeded(),
            "interrupted pair update did not restore the previous version");
        const VirtualPath copied_path = fixture.path("vehicle_copy.asset");
        const auto copied_id = pair_store.copy(published_path, copied_path);
        const auto copied_pair = read_asset_pair(types, fixture.files, copied_path);
        check(copied_id.succeeded() && copied_pair.succeeded() &&
            copied_pair.value().description.index.asset_id == copied_id.value() &&
            !(copied_id.value() == model_id) &&
            copied_pair.value().meta.segments[0].bytes == vertices,
            "pair copy did not assign a new root ID and preserve processed data");
        check(pair_store.remove(copied_path).succeeded() &&
            !fixture.files.stat(copied_path).succeeded() &&
            !fixture.files.stat(fixture.path("vehicle_copy.meta")).succeeded() &&
            pair_store.recover_tree(pair_root.value()).succeeded(),
            "pair deletion left a descriptor, meta or transaction");
        const VirtualPath moved_pair_path = fixture.path("vehicle_moved.asset");
        check(!pair_store.move(published_path, yaml_path).succeeded() &&
            read_asset_pair(types, fixture.files, published_path).succeeded(),
            "move into an existing descriptor changed the source");
        check(pair_store.move(published_path, moved_pair_path).succeeded() &&
            !fixture.files.stat(published_path).succeeded() &&
            !fixture.files.stat(fixture.path("vehicle_published.meta")).succeeded() &&
            read_asset_pair(types, fixture.files, moved_pair_path).succeeded() &&
            pair_store.recover_tree(pair_root.value()).succeeded(),
            "pair move did not preserve identity and remove both old files");
        auto file = encode_asset_file(file_index, {{"type_data", 1, true, type_bytes},
                                                   {model.geometry_segment, 2, false, vertices}});
        check(file.succeeded(), "model asset file encode failed");
        const VirtualPath model_path = fixture.path("vehicle.asset");
        check(fixture.files.write_binary(model_path, file.value(), FileWriteMode::CreateNew).succeeded(),
              "model asset file write failed");
        const auto summary = inspect_asset(fixture.files, model_path);
        check(summary.succeeded() && summary.value().dependencies.size() == 2 &&
              summary.value().subresources.size() == 1 && summary.value().segments.size() == 2,
              "model index did not expose dependencies, subresources and blob");
        auto validate_model = [&](const ModelAssetData& candidate)
        {
            if (candidate.source_uri.empty() || candidate.import_settings.unit_scale <= 0.0f)
                return invalid("source_uri", "invalid model import settings");
            if (!references.resolve(candidate.skeleton, "skeleton").succeeded())
                return invalid("skeleton", "skeleton reference missing");
            for (const ModelNodeData& item : candidate.nodes)
                if (!references.resolve(item.material_override, "nodes.material_override").succeeded() ||
                    item.mesh_subresource != mesh_id.hex())
                    return invalid("nodes", "node override target is missing");
            const auto segment = std::find_if(summary.value().segments.begin(), summary.value().segments.end(),
                [&](const AssetSegment& item) { return item.name == candidate.geometry_segment; });
            if (segment == summary.value().segments.end())
                return invalid("geometry_segment", "required Cook geometry is missing");
            return AssetStatus::success();
        };
        SchemaMigrationRegistry migrations;
        ModelAssetData loaded;
        check(load_asset(types, migrations, fixture.files, model_path, "toy3d.ModelAssetData",
                         loaded, validate_model).succeeded() && loaded.nodes.size() == 1 &&
              loaded.nodes[0].mesh_subresource == mesh_id.hex(),
              "model typed data failed to load alongside a large geometry blob");
        ModelNodeData second = node;
        second.id = "node-b";
        second.source_key = "vehicle/wheel";
        loaded.nodes.push_back(second);
        std::vector<ImportedSubresource> previous = {{"vehicle/body", mesh_id}};
        auto matches = match_subresources(previous, {"vehicle/wheel", "vehicle/body"});
        check(matches.succeeded() && matches.value().matched.size() == 1 &&
              matches.value().matched[0].id == mesh_id,
              "model node reorder changed mesh subresource identity");
        auto no_geometry = encode_asset_file(file_index, {{"type_data", 1, true, type_bytes}});
        const VirtualPath missing_path = fixture.path("vehicle_missing_cook.asset");
        check(no_geometry.succeeded() && fixture.files.write_binary(missing_path, no_geometry.value(),
              FileWriteMode::CreateNew).succeeded(), "missing Cook fixture failed");
        ModelAssetData unchanged = loaded;
        auto reject_missing = [](const ModelAssetData& candidate)
        {
            return invalid("geometry_segment", candidate.geometry_segment.empty() ?
                           "geometry segment name absent" : "required Cook geometry is missing");
        };
        check(load_asset(types, migrations, fixture.files, missing_path, "toy3d.ModelAssetData",
                         unchanged, reject_missing).code == AssetErrorCode::Value &&
              unchanged.nodes.size() == 2,
              "missing Cook product published a model candidate");
    }

    toy3d::AssetStatus validate_animation(const toy3d::AnimationAssetData& animation,
                                           const toy3d::AssetIndex& references)
    {
        using namespace toy3d;
        if (!references.resolve(animation.skeleton, "skeleton").succeeded())
            return invalid("skeleton", "skeleton is missing");
        if (!std::isfinite(animation.duration) || animation.duration <= 0.0f)
            return invalid("duration", "duration must be positive");
        std::set<std::string> track_ids;
        for (std::size_t track_index = 0; track_index < animation.tracks.size(); ++track_index)
        {
            const AnimationTrack& track = animation.tracks[track_index];
            SubresourceId target;
            AssetRef target_ref = animation.skeleton;
            target_ref.expected_type = "toy3d.Bone";
            if (!track_ids.insert(track.id).second ||
                !SubresourceId::parse(track.target_id, target))
                return invalid("tracks[" + std::to_string(track_index) + "].target_id",
                               "track target is missing or repeated");
            target_ref.subresource_id = target;
            if (!references.resolve(target_ref).succeeded())
                return invalid("tracks[" + std::to_string(track_index) + "].target_id",
                               "track target is missing or has the wrong type");
            float previous = -1.0f;
            for (std::size_t key_index = 0; key_index < track.keys.size(); ++key_index)
            {
                const float time = track.keys[key_index].time;
                if (!std::isfinite(time) || time < 0.0f || time > animation.duration || time < previous)
                    return invalid("tracks[" + std::to_string(track_index) + "].keys[" +
                        std::to_string(key_index) + "].time", "animation keys are unsorted or out of range");
                previous = time;
            }
        }
        for (std::size_t index = 0; index < animation.events.size(); ++index)
            if (animation.events[index].time < 0.0f || animation.events[index].time > animation.duration)
                return invalid("events[" + std::to_string(index) + "].time", "event is outside clip");
        return AssetStatus::success();
    }

    void test_animation(const toy3d::TypeRegistry& types, TestFiles& fixture)
    {
        using namespace toy3d;
        const AssetId skeleton_id = id("22223333444455556666777788889999");
        const SubresourceId bone_id = sub_id("3333444455556666777788889999aaaa");
        AssetFileIndex skeleton;
        skeleton.asset_id = skeleton_id;
        skeleton.root_type = "toy3d.SkeletonAsset";
        skeleton.subresources.push_back({bone_id, "toy3d.Bone"});
        AssetIndex references;
        check(references.add(fixture.path("rig.asset"), skeleton).succeeded(),
              "animation skeleton index failed");
        AnimationAssetData animation;
        animation.skeleton = reference(skeleton_id, "toy3d.SkeletonAsset");
        animation.duration = 2.0f;
        AnimationTrack track;
        track.id = "walk/root";
        track.target_id = bone_id.hex();
        track.interpolation = InterpolationMode::Linear;
        track.keys = {{0.0f, Vector3{0.0f, 0.0f, 0.0f}}, {2.0f, Vector3{1.0f, 0.0f, 0.0f}}};
        animation.tracks.push_back(track);
        animation.events.push_back({"step-left", 0.5f, "Footstep"});
        const std::vector<std::uint8_t> bytes = encoded(animation);
        check(validate_animation(animation, references).succeeded(),
              "valid animation was rejected");
        animation.tracks[0].keys[1].time = -1.0f;
        check(validate_animation(animation, references).property_path == "tracks[0].keys[1].time",
              "invalid animation key lost its location");
        animation.tracks[0].keys[1].time = 2.0f;
        animation.tracks[0].target_id = "missing-bone";
        check(validate_animation(animation, references).property_path == "tracks[0].target_id",
              "missing animation target was accepted");
        animation.tracks[0].target_id = bone_id.hex();
        animation.events[0].time = 3.0f;
        check(validate_animation(animation, references).property_path == "events[0].time",
              "out-of-range animation event was accepted");
        animation.events[0].time = 0.5f;
        animation.tracks[0].keys[1].time = std::numeric_limits<float>::infinity();
        ValueWriter nonfinite;
        const ValueStatus invalid_float = encode_value(nonfinite, animation);
        check(invalid_float.code == ValueErrorCode::InvalidValue &&
              invalid_float.property_path.find("keys[1].time") != std::string::npos,
              "nonfinite animation key was serialized");
        AssetFileIndex index;
        index.asset_id = id("444455556666777788889999aaaabbbb");
        index.root_type = "toy3d.AnimationAssetData";
        index.schema_version = 1;
        index.dependencies.push_back(animation.skeleton);
        auto file = encode_asset_file(index, {{"type_data", 1, true, bytes}});
        const VirtualPath path = fixture.path("walk.asset");
        check(file.succeeded() && fixture.files.write_binary(path, file.value(), FileWriteMode::CreateNew).succeeded(),
              "animation file fixture failed");
        SchemaMigrationRegistry migrations;
        AnimationAssetData loaded;
        check(load_asset(types, migrations, fixture.files, path, "toy3d.AnimationAssetData", loaded,
                         [&](const AnimationAssetData& value)
                         { return validate_animation(value, references); }).succeeded() &&
              loaded.tracks[0].keys.size() == 2,
              "animation typed load failed");
    }

    toy3d::AssetStatus validate_collision(const toy3d::CollisionAssetData& collision,
                                           const toy3d::AssetIndex& references)
    {
        using namespace toy3d;
        std::set<std::string> ids;
        for (std::size_t index = 0; index < collision.shapes.size(); ++index)
        {
            const CollisionShapeData& item = collision.shapes[index];
            const std::string path = "shapes[" + std::to_string(index) + "]";
            if (item.id.empty() || !ids.insert(item.id).second)
                return invalid(path + ".id", "collision shape ID is duplicate or empty");
            // C++17 get_if makes each supported shape branch and its invariant explicit.
            if (const BoxShape* box = std::get_if<BoxShape>(&item.shape))
            {
                if (box->half_extents.x <= 0.0f || box->half_extents.y <= 0.0f ||
                    box->half_extents.z <= 0.0f)
                    return invalid(path + ".shape.half_extents", "box half extents must be positive");
            }
            else if (const SphereShape* sphere = std::get_if<SphereShape>(&item.shape))
            {
                if (sphere->radius <= 0.0f)
                    return invalid(path + ".shape.radius", "sphere radius must be positive");
            }
            else if (const CapsuleShape* capsule = std::get_if<CapsuleShape>(&item.shape))
            {
                if (capsule->radius <= 0.0f || capsule->half_height < capsule->radius)
                    return invalid(path + ".shape.radius", "capsule radius or half height is invalid");
            }
            else if (const MeshShape* mesh = std::get_if<MeshShape>(&item.shape))
            {
                if (!references.resolve(mesh->mesh, path + ".shape.mesh").succeeded())
                    return invalid(path + ".shape.mesh", "collision mesh asset is missing");
            }
            else return invalid(path + ".shape", "unsupported collision branch");
        }
        return AssetStatus::success();
    }

    void test_collision(const toy3d::TypeRegistry& types)
    {
        using namespace toy3d;
        const AssetId mesh_asset_id = id("55556666777788889999aaaabbbbcccc");
        AssetFileIndex mesh_index;
        mesh_index.asset_id = mesh_asset_id;
        mesh_index.root_type = "toy3d.MeshAsset";
        AssetIndex references;
        auto mesh_path = VirtualPath::parse("/asset/meshes/wheel.asset");
        check(mesh_path.succeeded() && references.add(mesh_path.value(), mesh_index).succeeded(),
              "collision mesh index failed");
        CollisionAssetData collision;
        BoxShape box;
        box.half_extents = Vector3{1.0f, 2.0f, 3.0f};
        SphereShape sphere;
        sphere.radius = 0.5f;
        CapsuleShape capsule;
        capsule.radius = 0.25f;
        capsule.half_height = 1.0f;
        MeshShape mesh;
        mesh.mesh = reference(mesh_asset_id, "toy3d.MeshAsset");
        mesh.convex = true;
        collision.shapes = {{"box", box}, {"sphere", sphere}, {"capsule", capsule}, {"convex", mesh}};
        const std::vector<std::uint8_t> bytes = encoded(collision);
        check(validate_collision(collision, references).succeeded(),
              "valid collision branches were rejected");
        std::get<CapsuleShape>(collision.shapes[2].shape).radius = 0.0f;
        check(validate_collision(collision, references).property_path == "shapes[2].shape.radius",
              "nonpositive capsule radius was accepted");
        std::vector<std::uint8_t> unknown = bytes;
        const std::string tag = "toy3d.CapsuleShape";
        auto tag_at = std::search(unknown.begin(), unknown.end(), tag.begin(), tag.end());
        check(tag_at != unknown.end(), "collision branch fixture tag was absent");
        *tag_at = 'x';
        ValueReader unknown_reader(unknown);
        CollisionAssetData unchanged = collision;
        check(decode_value(unknown_reader, unchanged).code == ValueErrorCode::InvalidValue &&
              std::get<CapsuleShape>(unchanged.shapes[2].shape).radius == 0.0f,
              "unknown required shape branch was decoded or changed caller value");
    }

    toy3d::AssetStatus validate_scene(const toy3d::SceneAssetData& scene)
    {
        using namespace toy3d;
        std::map<std::string, const SceneActorData*> actors;
        std::set<std::string> all_components;
        for (std::size_t index = 0; index < scene.actors.size(); ++index)
        {
            const SceneActorData& actor = scene.actors[index];
            if (actor.id.empty() || actor.type.empty() || !actors.emplace(actor.id, &actor).second)
                return invalid("actors[" + std::to_string(index) + "].id", "actor ID or type is invalid");
            bool has_root = false;
            for (const SceneComponentData& component : actor.components)
            {
                if (component.id.empty() || component.type.empty() ||
                    !all_components.insert(component.id).second)
                    return invalid("actors[" + std::to_string(index) + "].components",
                                   "component identity is duplicate or invalid");
                if (component.id == actor.root_component_id) has_root = true;
            }
            if (!has_root)
                return invalid("actors[" + std::to_string(index) + "].root_component_id",
                               "root component does not belong to actor");
        }
        for (std::size_t index = 0; index < scene.actors.size(); ++index)
        {
            const SceneActorData& actor = scene.actors[index];
            if (actor.parent_actor_id.empty())
            {
                if (!actor.parent_component_id.empty())
                    return invalid("actors[" + std::to_string(index) + "].parent_component_id",
                                   "parent component has no actor");
                continue;
            }
            const auto parent = actors.find(actor.parent_actor_id);
            if (parent == actors.end())
                return invalid("actors[" + std::to_string(index) + "].parent_actor_id",
                               "attachment parent actor is missing");
            const auto component = std::find_if(parent->second->components.begin(),
                                                parent->second->components.end(),
                [&](const SceneComponentData& item) { return item.id == actor.parent_component_id; });
            if (component == parent->second->components.end())
                return invalid("actors[" + std::to_string(index) + "].parent_component_id",
                               "attachment parent component is missing");
            std::set<std::string> ancestors;
            const SceneActorData* cursor = &actor;
            while (!cursor->parent_actor_id.empty())
            {
                if (!ancestors.insert(cursor->id).second)
                    return invalid("actors[" + std::to_string(index) + "].parent_actor_id",
                                   "actor attachment cycle");
                const auto next = actors.find(cursor->parent_actor_id);
                if (next == actors.end()) break;
                cursor = next->second;
            }
        }
        return AssetStatus::success();
    }

    void test_scene(const toy3d::TypeRegistry& types, TestFiles& fixture)
    {
        using namespace toy3d;
        const AssetId mesh_id = id("55556666777788889999aaaabbbbcccc");
        SceneComponentData root_component;
        root_component.id = "component-root";
        root_component.type = "Transform";
        root_component.asset = reference(mesh_id, "toy3d.MeshAsset");
        SceneComponentData child_component = root_component;
        child_component.id = "component-child";
        SceneActorData root;
        root.id = "actor-root";
        root.type = "StaticMeshActor";
        root.components.push_back(root_component);
        root.root_component_id = root_component.id;
        SceneActorData child;
        child.id = "actor-child";
        child.type = "StaticMeshActor";
        child.components.push_back(child_component);
        child.root_component_id = child_component.id;
        child.parent_actor_id = root.id;
        child.parent_component_id = root_component.id;
        SceneAssetData scene;
        scene.actors = {root, child};
        const std::vector<std::uint8_t> bytes = encoded(scene);
        check(validate_scene(scene).succeeded(), "valid scene graph was rejected");
        scene.actors[1].id = root.id;
        check(validate_scene(scene).property_path == "actors[1].id", "duplicate Actor ID was accepted");
        scene.actors[1] = child;
        scene.actors[1].parent_component_id = "missing-component";
        check(validate_scene(scene).property_path == "actors[1].parent_component_id",
              "missing attachment component was accepted");
        scene.actors[1] = child;
        scene.actors[0].parent_actor_id = child.id;
        scene.actors[0].parent_component_id = child_component.id;
        check(validate_scene(scene).message == "actor attachment cycle", "actor attachment cycle was accepted");
        AssetFileIndex index;
        index.asset_id = id("6666777788889999aaaabbbbccccdddd");
        index.root_type = "toy3d.SceneAssetData";
        index.schema_version = 1;
        auto valid_file = encode_asset_file(index, {{"type_data", 1, true, bytes}});
        const VirtualPath path = fixture.path("level.asset");
        check(valid_file.succeeded() && fixture.files.write_binary(path, valid_file.value(),
              FileWriteMode::CreateNew).succeeded(), "scene file fixture failed");
        SchemaMigrationRegistry migrations;
        SceneAssetData loaded;
        check(load_asset(types, migrations, fixture.files, path, "toy3d.SceneAssetData",
                         loaded, validate_scene).succeeded() && loaded.actors.size() == 2,
              "scene typed data failed to load");
        ValueWriter invalid_writer;
        check(encode_value(invalid_writer, scene).succeeded(), "invalid scene fixture encode failed");
        auto invalid_file = encode_asset_file(index, {{"type_data", 1, true, invalid_writer.bytes()}});
        check(invalid_file.succeeded() && fixture.files.write_binary(path, invalid_file.value(),
              FileWriteMode::Truncate).succeeded(), "invalid scene fixture write failed");
        check(load_asset(types, migrations, fixture.files, path, "toy3d.SceneAssetData",
                         loaded, validate_scene).code == AssetErrorCode::Value &&
              loaded.actors[0].parent_actor_id.empty(),
              "invalid scene candidate replaced the previous scene");
    }

    toy3d::AssetStatus validate_material(const toy3d::MaterialAssetData& material,
                                          const toy3d::shader::ShaderAsset& shader_asset)
    {
        using namespace toy3d;
        std::map<shader::ShaderParameterId, const shader::Property*> authoritative;
        for (const shader::Property& property : shader_asset.properties)
        {
            shader::ShaderParameterCategory category = shader::ShaderParameterCategory::Constant;
            if (property.type == shader::PropertyType::Texture2D ||
                property.type == shader::PropertyType::TextureCube)
                category = shader::ShaderParameterCategory::SampledTexture;
            else if (property.type == shader::PropertyType::Sampler ||
                     property.type == shader::PropertyType::ComparisonSampler)
                category = shader::ShaderParameterCategory::Sampler;
            const auto parameter_id = shader::make_shader_parameter_id(shader::BindingGroup::Material,
                                                                       category, property.name);
            authoritative.emplace(parameter_id, &property);
        }
        for (std::size_t index = 0; index < material.overrides.size(); ++index)
        {
            const MaterialOverrideData& override_value = material.overrides[index];
            const std::string path = "overrides[" + std::to_string(index) + "]";
            const auto found = authoritative.find(override_value.parameter_id);
            if (found == authoritative.end())
                return invalid(path + ".parameter_id", "orphan Material override remains unresolved");
            if (found->second->name != override_value.parameter_name ||
                found->second->type != shader::PropertyType::Range ||
                override_value.declared_type != "Range")
                return invalid(path + ".declared_type", "Material override type disagrees with Shader Properties");
            if (found->second->range_min && override_value.scalar < *found->second->range_min)
                return invalid(path + ".scalar", "Material scalar is below Shader range");
            if (found->second->range_max && override_value.scalar > *found->second->range_max)
                return invalid(path + ".scalar", "Material scalar is above Shader range");
        }
        return AssetStatus::success();
    }

    void test_material(const toy3d::TypeRegistry& types)
    {
        using namespace toy3d;
        std::ifstream input(TOY3D_RESOURCE_SHADER_FIXTURE, std::ios::binary);
        check(input.good(), "Shader Properties fixture could not be opened");
        const std::string source(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
        const shader::ParseResult parsed = shader::parse_shader(source, TOY3D_RESOURCE_SHADER_FIXTURE);
        check(parsed.succeeded() && parsed.asset && parsed.asset->properties.size() == 3,
              "Shader Properties fixture did not parse");
        const shader::ShaderAsset& shader_asset = *parsed.asset;
        const auto property = std::find_if(shader_asset.properties.begin(), shader_asset.properties.end(),
            [](const shader::Property& item) { return item.name == "roughness"; });
        check(property != shader_asset.properties.end() && property->type == shader::PropertyType::Range,
              "roughness property missing from Shader schema");
        const shader::ShaderParameterId parameter_id = shader::make_shader_parameter_id(
            shader::BindingGroup::Material, shader::ShaderParameterCategory::Constant, property->name);
        check(parameter_id != 0, "Shader parameter identity is invalid");
        MaterialAssetData material;
        material.shader = reference(id("777788889999aaaabbbbccccddddeeee"), "toy3d.ShaderAsset");
        material.overrides.push_back({parameter_id, "roughness", "Range", 0.7f});
        const std::vector<std::uint8_t> bytes = encoded(material);
        check(validate_material(material, shader_asset).succeeded(),
              "valid Material override disagreed with Shader Properties");
        shader::ShaderAsset reordered = shader_asset;
        std::reverse(reordered.properties.begin(), reordered.properties.end());
        check(validate_material(material, reordered).succeeded(),
              "Shader property order changed Material parameter binding");
        shader::ShaderAsset removed = shader_asset;
        removed.properties.erase(std::remove_if(removed.properties.begin(), removed.properties.end(),
            [](const shader::Property& item) { return item.name == "roughness"; }),
            removed.properties.end());
        check(validate_material(material, removed).property_path == "overrides[0].parameter_id" &&
              encoded(material) == bytes,
              "orphan override was lost or rebound to another Shader parameter");
        material.overrides[0].declared_type = "Float";
        check(validate_material(material, shader_asset).property_path == "overrides[0].declared_type",
              "Material override type mismatch was accepted");
        material.overrides[0].declared_type = "Range";
        const TypeDesc* type = types.find("toy3d.MaterialAssetData");
        check(type != nullptr, "Material fixture schema is missing");
        int preview_notifications = 0;
        auto path = VirtualPath::parse("/asset/material.asset");
        check(path.succeeded(), "Material fixture path failed");
        EditSession<MaterialAssetData> session(types, *type,
            id("88889999aaaabbbbccccddddeeeeffff"), path.value(), material,
            [&](const MaterialAssetData& candidate) { return validate_material(candidate, shader_asset); },
            nullptr, {}, [&](const EditRecord&) { ++preview_notifications; });
        ValueWriter scalar;
        check(scalar.write_float32(0.8f).succeeded(), "Material scalar fixture failed");
        const PropertyPath scalar_path = {PropertyPathPart::field("overrides"),
            PropertyPathPart::index_at(0), PropertyPathPart::field("scalar")};
        check(session.apply_edit({{scalar_path, scalar.bytes(), EditChangeKind::Setter}}).succeeded() &&
              session.value().overrides[0].scalar == 0.8f && preview_notifications == 1,
              "Material scalar edit did not use a typed fake preview adapter");
        ValueWriter out_of_range;
        check(out_of_range.write_float32(2.0f).succeeded() &&
              !session.apply_edit({{scalar_path, out_of_range.bytes()}}).succeeded() &&
              preview_notifications == 1,
              "Shader range violation changed Material preview");
    }
} // namespace

int main()
{
    toy3d::TypeRegistry types;
    check(toy3d::register_generated_fixture_types(types).succeeded() && types.freeze().succeeded(),
          "resource kind schema registration failed");
    TestFiles fixture;
    test_model(types, fixture);
    test_animation(types, fixture);
    test_collision(types);
    test_scene(types, fixture);
    test_material(types);
    return 0;
}
