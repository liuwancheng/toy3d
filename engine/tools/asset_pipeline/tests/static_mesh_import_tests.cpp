#include "asset_pipeline/static_mesh_import.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "asset/asset_pair_store.h"
#include "asset/asset_descriptor_path.h"
#include "asset/mesh/static_mesh_asset.h"
#include "asset_pipeline/static_mesh_builder.h"

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
    toy3d::VirtualPath path(const std::string& text)
    {
        const auto result = toy3d::VirtualPath::parse(text);
        require(result.succeeded(), "path parse failed");
        return result.value();
    }
    void mount(toy3d::NativePlatformFile& platform, toy3d::FileSystem& files, const char* root,
               const toy3d::PhysicalPath& physical, bool writable)
    {
        toy3d::DirectoryFileStoreDesc desc;
        desc.physical_root = physical;
        desc.writable = writable;
        const auto store = toy3d::DirectoryFileStore::create(platform, desc);
        require(store.succeeded(), "store create failed");
        toy3d::FileMountDesc mount;
        mount.virtual_root = path(root);
        mount.store = store.value();
        mount.access = writable ? toy3d::MountAccess::ReadWrite : toy3d::MountAccess::ReadOnly;
        require(files.add_mount(mount).succeeded(), "mount failed");
    }
    void replace_text(toy3d::FileSystem& files, const std::string& name, const std::string& text)
    {
        const std::vector<std::uint8_t> bytes(text.begin(), text.end());
        require(files.write_binary_atomic(path(name), bytes, toy3d::FilePublishMode::Replace).succeeded(),
                "fixture write failed");
    }
    void clear_asset_fixture(toy3d::FileSystem& files, const toy3d::VirtualPath& asset)
    {
        // These exact paths belong to this test. Older schema outputs cannot be
        // decoded by AssetPairStore, so remove both members before regeneration.
        toy3d::VirtualPath meta;
        require(toy3d::asset_meta_path(asset, meta), "fixture meta path failed");
        for (const toy3d::VirtualPath& file : {asset, meta})
        {
            const auto found = files.stat(file);
            if (found.succeeded())
            {
                require(files.remove_file(file).succeeded(), "old fixture cleanup failed");
            }
            else
            {
                require(found.status().code == toy3d::FileErrorCode::NotFound, "fixture stat failed");
            }
        }
    }
    std::string fbx_fixture(int up_axis, int front_axis, int front_sign, double units, bool mirror)
    {
        return "; FBX 7.4.0 project file\n"
               "FBXHeaderExtension: {\n FBXHeaderVersion: 1003\n FBXVersion: 7400\n}\n"
               "GlobalSettings: {\n Version: 1000\n Properties70: {\n"
               "P: \"UpAxis\", \"int\", \"Integer\", \"\", " +
               std::to_string(up_axis) +
               "\n"
               "P: \"UpAxisSign\", \"int\", \"Integer\", \"\", 1\n"
               "P: \"FrontAxis\", \"int\", \"Integer\", \"\", " +
               std::to_string(front_axis) +
               "\n"
               "P: \"FrontAxisSign\", \"int\", \"Integer\", \"\", " +
               std::to_string(front_sign) +
               "\n"
               "P: \"CoordAxis\", \"int\", \"Integer\", \"\", 0\n"
               "P: \"CoordAxisSign\", \"int\", \"Integer\", \"\", 1\n"
               "P: \"UnitScaleFactor\", \"double\", \"Number\", \"\", " +
               std::to_string(units) +
               "\n"
               "}\n}\nObjects: {\n"
               "Geometry: 1, \"Geometry::Triangle\", \"Mesh\" {\n"
               "Vertices: *9 { a: 0,0,0,100,0,0,0,200,300 }\n"
               "PolygonVertexIndex: *3 { a: 0,1,-3 }\n}\n"
               "Model: 2, \"Model::Triangle\", \"Mesh\" {\n Version: 232\n Properties70: {\n"
               "P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\", 10,20,30\n"
               "P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\", " +
               std::string(mirror ? "-1,2,1" : "1,2,1") +
               "\n}\n}\n}\n"
               "Connections: {\n C: \"OO\",1,2\n C: \"OO\",2,0\n}\n";
    }
} // namespace

int main()
{
    try
    {
        using namespace toy3d;
        NativePlatformFile platform;
        const PhysicalPath output_root(TOY3D_IMPORT_TEST_OUTPUT);
        require(platform.create_directories(output_root).succeeded(), "output directory create failed");
        FileSystem files;
        mount(platform, files, "/Samples", PhysicalPath(TOY3D_IMPORT_TEST_SAMPLES), false);
        mount(platform, files, "/Output", output_root, true);
        require(files.freeze().succeeded(), "freeze failed");
        TypeRegistry types;
        require(register_static_mesh_asset_types(types).succeeded() && types.freeze().succeeded(),
                "static mesh schema registration failed");
        AssetPairStore assets(types, files);
        AssetId id;
        require(AssetId::parse("1234567890abcdef1234567890abcdef", id), "id parse failed");
        AssetId fresh;
        require(AssetId::try_generate(fresh) && fresh.valid(), "id generation failed");
        const StaticMeshImportOptions file_unit_options;
        for (const char* filename : {"triangle.obj", "phong_cube.fbx", "triangle.gltf", "triangle.glb"})
        {
            StaticMeshImportOptions options;
            options.use_file_unit = std::string(filename) == "phong_cube.fbx";
            options.source_unit_in_centimeters = options.use_file_unit ? 1.0f : 100.0f;
            const auto imported =
                import_static_mesh_asset(files, path(std::string("/Samples/") + filename), id, options);
            if (!imported.succeeded())
            {
                std::cerr << filename << ": " << imported.status().message << '\n';
            }
            require(imported.succeeded(), "sample import failed");
            const auto repeated =
                import_static_mesh_asset(files, path(std::string("/Samples/") + filename), id, options);
            require(repeated.succeeded() && repeated.value().pair.asset == imported.value().pair.asset &&
                        repeated.value().pair.meta == imported.value().pair.meta,
                    "nondeterministic import");
            const VirtualPath output = path(std::string("/Output/") + filename + ".asset");
            clear_asset_fixture(files, output);
            require(assets.publish(output, imported.value().pair, FilePublishMode::CreateNew).succeeded(),
                    "publish failed");
            const auto loaded = read_static_mesh_asset(files, output);
            require(loaded.succeeded() && !loaded.value().sections.empty(), "asset load failed");
            if (!options.use_file_unit)
            {
                // OBJ/glTF have no implicit backend scale: caller controls it.
                options.convert_scene_unit = false;
                const auto raw = import_static_meshes(files, path(std::string("/Samples/") + filename), options);
                require(raw.succeeded(), "unit bypass failed");
                const auto raw_geometry = build_static_mesh(raw.value()[0].mesh);
                require(raw_geometry.succeeded() &&
                            raw_geometry.value().vertices.size() == loaded.value().vertices.size(),
                        "unit conversion changed topology");
                for (std::size_t vertex = 0; vertex < loaded.value().vertices.size(); ++vertex)
                {
                    require(is_nearly_equal(loaded.value().vertices[vertex].position,
                                            raw_geometry.value().vertices[vertex].position * 100.0f),
                            "explicit source unit ignored");
                }
            }
            std::cout << filename << " vertices=" << loaded.value().vertices.size()
                      << " indices=" << loaded.value().indices.size() << '\n';
        }

        MeshDescription source;
        source.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        source.material_slots = {"A", "B"};
        source.corners = {{0, {0, 0, 1}, {0, 0}},  {1, {0, 0, 1}, {1, 0}},  {2, {0, 0, 1}, {0, 1}},
                          {0, {0, 0, -1}, {1, 1}}, {2, {0, 0, -1}, {1, 0}}, {1, {0, 0, -1}, {0, 1}}};
        source.triangles = {{{0, 1, 2}, 0}, {{3, 4, 5}, 1}};
        const auto built = build_static_mesh(source);
        require(built.succeeded() && built.value().vertices.size() == 6 && built.value().sections.size() == 2,
                "UV seam/hard edge or material group lost");
        require(built.value().vertices[0].uv0 != built.value().vertices[3].uv0, "corner UV was welded");
        const auto encoded_source = encode_mesh_description(source);
        require(encoded_source.succeeded() && decode_mesh_description(encoded_source.value()).succeeded(),
                "source codec failed");
        auto bad_source = source;
        bad_source.corners[0].vertex = 9;
        require(!build_static_mesh(bad_source).succeeded(), "bad source accepted");
        const auto encoded = encode_static_mesh_geometry(built.value());
        require(encoded.succeeded(), "geometry codec failed");
        auto old_geometry = encoded.value();
        old_geometry[0] = 1;
        require(!decode_static_mesh_geometry(old_geometry).succeeded(), "meter geometry version accepted");
        auto truncated = encoded.value();
        truncated.pop_back();
        require(!decode_static_mesh_geometry(truncated).succeeded(), "truncation accepted");
        auto bad_geometry = built.value();
        bad_geometry.sections[0].first_index = 1;
        require(!encode_static_mesh_geometry(bad_geometry).succeeded(), "bad section accepted");
        bad_geometry = built.value();
        bad_geometry.vertices[0].position.x = std::numeric_limits<float>::infinity();
        require(!encode_static_mesh_geometry(bad_geometry).succeeded(), "infinity accepted");
        bad_geometry = built.value();
        bad_geometry.indices[2] = bad_geometry.indices[0];
        require(!encode_static_mesh_geometry(bad_geometry).succeeded(), "degenerate face accepted");

        // Disk loading is independent of the external source file and importer.
        replace_text(files, "/Output/local.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        const auto local = import_static_mesh_asset(files, path("/Output/local.obj"), id, file_unit_options);
        require(local.succeeded(), "local import failed");
        const VirtualPath new_asset = path("/Output/create_new.asset");
        clear_asset_fixture(files, new_asset);
        require(assets.publish(new_asset, local.value().pair, FilePublishMode::CreateNew).succeeded(),
                "first creation failed");
        require(!assets.publish(new_asset, local.value().pair, FilePublishMode::CreateNew).succeeded(),
                "overwrite accepted");
        require(files.remove_file(path("/Output/local.obj")).succeeded(), "source removal failed");
        require(read_static_mesh_asset(files, new_asset).succeeded(), "source-free load failed");

        // Distinct collinear vertices, coincident positions and repeated indices
        // must be removed before corners are emitted, without losing the valid face.
        replace_text(files, "/Output/mixed_degenerate.obj",
                     "o Mixed\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 2 0 0\nv 0 0 0\n"
                     "vt 0 0\nvt 1 0\nvt 0 1\n"
                     "f 1 2 4\nf 1 2 5\nf 1 1 2\nf 1/1 2/2 3/3\n");
        const auto mixed = import_static_meshes(files, path("/Output/mixed_degenerate.obj"), file_unit_options);
        require(mixed.succeeded(), "mixed valid/degenerate geometry rejected");
        require(mixed.value()[0].mesh.triangles.size() == 1 && mixed.value()[0].mesh.corners.size() == 3,
                "degenerate faces left corners or removed the valid triangle");
        bool warned = false;
        for (const auto& warning : mixed.value()[0].warnings)
        {
            warned = warned || warning.find("Mesh 'Mixed': skipped 3 degenerate triangles.") != std::string::npos;
        }
        require(warned, "skipped triangle count and source mesh name were not reported");
        const auto& mixed_corners = mixed.value()[0].mesh.corners;
        require(mixed_corners[0].uv0 == Vector2(0, 0) &&
                    ((mixed_corners[1].uv0 == Vector2(1, 0) && mixed_corners[2].uv0 == Vector2(0, 1)) ||
                     (mixed_corners[1].uv0 == Vector2(0, 1) && mixed_corners[2].uv0 == Vector2(1, 0))),
                "surviving triangle lost UVs");
        const auto mixed_asset =
            import_static_mesh_asset(files, path("/Output/mixed_degenerate.obj"), id, file_unit_options);
        require(mixed_asset.succeeded(), "cleaned geometry did not build/encode");
        const auto repeated_mixed =
            import_static_mesh_asset(files, path("/Output/mixed_degenerate.obj"), id, file_unit_options);
        require(repeated_mixed.succeeded() && repeated_mixed.value().pair.asset == mixed_asset.value().pair.asset &&
                    repeated_mixed.value().pair.meta == mixed_asset.value().pair.meta &&
                    repeated_mixed.value().warnings == mixed_asset.value().warnings,
                "degenerate cleanup is nondeterministic");

        replace_text(files, "/Output/all_degenerate.obj", "v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n");
        const auto empty = import_static_meshes(files, path("/Output/all_degenerate.obj"), file_unit_options);
        require(!empty.succeeded() &&
                    empty.status().message == "no valid triangles remain after removing degenerate triangles",
                "all-degenerate geometry was accepted or did not diagnose missing valid triangles");

        replace_text(files, "/Output/degenerate_part.obj",
                     "o EmptyPart\nv 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n"
                     "o ValidPart\nv 0 1 0\nf 1 2 4\n");
        const auto part = import_static_mesh_asset(files, path("/Output/degenerate_part.obj"), id, file_unit_options);
        require(part.succeeded(), "an empty source part prevented valid combined geometry from importing");

        replace_text(files, "/Output/overflow.obj", "v 0 0 0\nv 1e20 0 0\nv 0 1e20 0\nf 1 2 3\n");
        require(!import_static_meshes(files, path("/Output/overflow.obj"), file_unit_options).succeeded(),
                "non-finite cross product was silently treated as removable degeneracy");

        replace_text(files, "/Output/axis.fbx", fbx_fixture(1, 2, -1, 1, false));
        const auto centimeter = import_static_meshes(files, path("/Output/axis.fbx"), file_unit_options);
        if (!centimeter.succeeded())
        {
            std::cerr << centimeter.status().message << '\n';
        }
        require(centimeter.succeeded(), "synthetic FBX failed");
        const Vector3 a = centimeter.value()[0].mesh.positions[0];
        const Vector3 b = centimeter.value()[0].mesh.positions[1];
        require(is_nearly_equal(a, Vector3(10, 20, -30)), "FBX node translation/unit conversion wrong");
        require(is_nearly_equal(b.x - a.x, 100.0f), "FBX units applied twice");
        replace_text(files, "/Output/axis.fbx", fbx_fixture(2, 1, 1, 100, true));
        const auto meter_z_up = import_static_meshes(files, path("/Output/axis.fbx"), file_unit_options);
        require(meter_z_up.succeeded(), "Z-up meter FBX failed");
        const MeshDescription& converted = meter_z_up.value()[0].mesh;
        require(is_nearly_equal(converted.positions[0], Vector3(1000, 3000, 2000)), "Z-up conversion wrong");
        const auto& triangle = converted.triangles[0];
        const Vector3 p0 = converted.positions[converted.corners[triangle.corners[0]].vertex];
        const Vector3 p1 = converted.positions[converted.corners[triangle.corners[1]].vertex];
        const Vector3 p2 = converted.positions[converted.corners[triangle.corners[2]].vertex];
        require(dot(cross(p1 - p0, p2 - p0), converted.corners[triangle.corners[0]].normal) > 0,
                "mirror winding/normal mismatch");
        StaticMeshImportOptions scaled_options;
        scaled_options.import_uniform_scale = 0.5f;
        const auto scaled = import_static_meshes(files, path("/Output/axis.fbx"), scaled_options);
        require(scaled.succeeded(), "uniform scale failed");
        for (std::size_t vertex = 0; vertex < converted.positions.size(); ++vertex)
        {
            require(is_nearly_equal(scaled.value()[0].mesh.positions[vertex], converted.positions[vertex] * 0.5f),
                    "uniform scale did not apply once to geometry and node translation");
        }
        scaled_options.convert_scene_unit = false;
        const auto bypass = import_static_meshes(files, path("/Output/axis.fbx"), scaled_options);
        require(bypass.succeeded() && is_nearly_equal(bypass.value()[0].mesh.positions[0], Vector3(5, 15, 10)),
                "unit bypass lost uniform scale or axis conversion");
        scaled_options.convert_scene_unit = true;
        scaled_options.use_file_unit = false;
        scaled_options.source_unit_in_centimeters = 2;
        const auto overridden = import_static_meshes(files, path("/Output/axis.fbx"), scaled_options);
        require(overridden.succeeded() && is_nearly_equal(overridden.value()[0].mesh.positions[0], Vector3(10, 30, 20)),
                "explicit unit override ignored");
        StaticMeshImportOptions bad_options;
        bad_options.import_uniform_scale = 0;
        require(!import_static_meshes(files, path("/Output/axis.fbx"), bad_options).succeeded(), "zero scale accepted");
        bad_options.import_uniform_scale = 1;
        bad_options.use_file_unit = false;
        bad_options.source_unit_in_centimeters = 0;
        require(!import_static_meshes(files, path("/Output/axis.fbx"), bad_options).succeeded(),
                "zero source unit accepted");
        bad_options.source_unit_in_centimeters = std::numeric_limits<float>::infinity();
        require(!import_static_meshes(files, path("/Output/axis.fbx"), bad_options).succeeded(),
                "infinite source unit accepted");
        bad_options.convert_scene_unit = false;
        require(import_static_meshes(files, path("/Output/axis.fbx"), bad_options).succeeded(),
                "disabled unit conversion must ignore the unused unit input");
        bad_options.convert_scene_unit = true;
        bad_options.source_unit_in_centimeters = 100;
        bad_options.import_uniform_scale = std::numeric_limits<float>::max();
        require(!import_static_meshes(files, path("/Output/axis.fbx"), bad_options).succeeded(),
                "overflowing scale accepted");
        replace_text(files, "/Output/broken.fbx", "broken");
        require(!import_static_mesh_asset(files, path("/Output/broken.fbx"), id, file_unit_options).succeeded(),
                "broken FBX accepted");
        std::cout << "StaticMesh import tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
