#include "asset_import/static_mesh_import.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "mesh_builder/static_mesh_builder.h"

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    toy3d::VirtualPath path(const std::string& text)
    {
        const auto result = toy3d::VirtualPath::parse(text);
        require(result.succeeded(), "path parse failed");
        return result.value();
    }
    void mount(toy3d::NativePlatformFile& platform, toy3d::FileSystem& files,
               const char* root, const toy3d::PhysicalPath& physical, bool writable)
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
        require(files.write_binary_atomic(path(name), bytes, toy3d::FilePublishMode::Replace).succeeded(), "fixture write failed");
    }
    std::string fbx_fixture(int up_axis, int front_axis, int front_sign, double units, bool mirror)
    {
        return "; FBX 7.4.0 project file\n"
            "FBXHeaderExtension: {\n FBXHeaderVersion: 1003\n FBXVersion: 7400\n}\n"
            "GlobalSettings: {\n Version: 1000\n Properties70: {\n"
            "P: \"UpAxis\", \"int\", \"Integer\", \"\", " + std::to_string(up_axis) + "\n"
            "P: \"UpAxisSign\", \"int\", \"Integer\", \"\", 1\n"
            "P: \"FrontAxis\", \"int\", \"Integer\", \"\", " + std::to_string(front_axis) + "\n"
            "P: \"FrontAxisSign\", \"int\", \"Integer\", \"\", " + std::to_string(front_sign) + "\n"
            "P: \"CoordAxis\", \"int\", \"Integer\", \"\", 0\n"
            "P: \"CoordAxisSign\", \"int\", \"Integer\", \"\", 1\n"
            "P: \"UnitScaleFactor\", \"double\", \"Number\", \"\", " + std::to_string(units) + "\n"
            "}\n}\nObjects: {\n"
            "Geometry: 1, \"Geometry::Triangle\", \"Mesh\" {\n"
            "Vertices: *9 { a: 0,0,0,100,0,0,0,200,300 }\n"
            "PolygonVertexIndex: *3 { a: 0,1,-3 }\n}\n"
            "Model: 2, \"Model::Triangle\", \"Mesh\" {\n Version: 232\n Properties70: {\n"
            "P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\", 10,20,30\n"
            "P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\", " + std::string(mirror ? "-1,2,1" : "1,2,1") + "\n}\n}\n}\n"
            "Connections: {\n C: \"OO\",1,2\n C: \"OO\",2,0\n}\n";
    }
}

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
        AssetId id;
        require(AssetId::parse("1234567890abcdef1234567890abcdef", id), "id parse failed");
        AssetId fresh;
        require(AssetId::try_generate(fresh) && fresh.valid(), "id generation failed");
        for (const char* filename : {"triangle.obj", "phong_cube.fbx", "triangle.gltf", "triangle.glb"})
        {
            const auto imported = import_static_mesh_asset(files, path(std::string("/Samples/") + filename), id);
            if (!imported.succeeded()) std::cerr << filename << ": " << imported.status().message << '\n';
            require(imported.succeeded(), "sample import failed");
            const auto repeated = import_static_mesh_asset(files, path(std::string("/Samples/") + filename), id);
            require(repeated.succeeded() && repeated.value().bytes == imported.value().bytes, "nondeterministic import");
            const VirtualPath output = path(std::string("/Output/") + filename + ".asset");
            require(files.write_binary_atomic(output, imported.value().bytes, FilePublishMode::Replace).succeeded(), "publish failed");
            const auto loaded = read_static_mesh_asset(files, output);
            require(loaded.succeeded() && !loaded.value().sections.empty(), "asset load failed");
            std::cout << filename << " vertices=" << loaded.value().vertices.size() << " indices=" << loaded.value().indices.size() << '\n';
        }

        MeshDescription source;
        source.positions = {{0,0,0}, {1,0,0}, {0,1,0}};
        source.material_slots = {"A", "B"};
        source.corners = {{0,{0,0,1},{0,0}}, {1,{0,0,1},{1,0}}, {2,{0,0,1},{0,1}},
                          {0,{0,0,-1},{1,1}}, {2,{0,0,-1},{1,0}}, {1,{0,0,-1},{0,1}}};
        source.triangles = {{{0,1,2},0}, {{3,4,5},1}};
        const auto built = build_static_mesh(source);
        require(built.succeeded() && built.value().vertices.size() == 6 && built.value().sections.size() == 2,
                "UV seam/hard edge or material group lost");
        require(built.value().vertices[0].uv0 != built.value().vertices[3].uv0, "corner UV was welded");
        const auto encoded_source = encode_mesh_description(source);
        require(encoded_source.succeeded() && decode_mesh_description(encoded_source.value()).succeeded(), "source codec failed");
        auto bad_source = source;
        bad_source.corners[0].vertex = 9;
        require(!build_static_mesh(bad_source).succeeded(), "bad source accepted");
        const auto encoded = encode_static_mesh_geometry(built.value());
        require(encoded.succeeded(), "geometry codec failed");
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
        const auto local = import_static_mesh_asset(files, path("/Output/local.obj"), id);
        require(local.succeeded(), "local import failed");
        const VirtualPath new_asset = path("/Output/create_new.asset");
        const auto existed = files.stat(new_asset);
        if (existed.succeeded()) require(files.remove_file(new_asset).succeeded(), "old fixture cleanup failed");
        require(files.write_binary_atomic(new_asset, local.value().bytes, FilePublishMode::CreateNew).succeeded(), "first creation failed");
        require(!files.write_binary_atomic(new_asset, local.value().bytes, FilePublishMode::CreateNew).succeeded(), "overwrite accepted");
        require(files.remove_file(path("/Output/local.obj")).succeeded(), "source removal failed");
        require(read_static_mesh_asset(files, new_asset).succeeded(), "source-free load failed");

        replace_text(files, "/Output/axis.fbx", fbx_fixture(1, 2, -1, 1, false));
        const auto centimeter = import_static_meshes(files, path("/Output/axis.fbx"));
        if (!centimeter.succeeded()) std::cerr << centimeter.status().message << '\n';
        require(centimeter.succeeded(), "synthetic FBX failed");
        const Vector3 a = centimeter.value()[0].mesh.positions[0];
        const Vector3 b = centimeter.value()[0].mesh.positions[1];
        require(is_nearly_equal(a, Vector3(.1f,.2f,-.3f)), "FBX node translation/unit conversion wrong");
        require(is_nearly_equal(b.x - a.x, 1.0f), "FBX units applied twice");
        replace_text(files, "/Output/axis.fbx", fbx_fixture(2, 1, 1, 100, true));
        const auto meter_z_up = import_static_meshes(files, path("/Output/axis.fbx"));
        require(meter_z_up.succeeded(), "Z-up meter FBX failed");
        const MeshDescription& converted = meter_z_up.value()[0].mesh;
        require(is_nearly_equal(converted.positions[0], Vector3(10,30,20)), "Z-up conversion wrong");
        const auto& triangle = converted.triangles[0];
        const Vector3 p0 = converted.positions[converted.corners[triangle.corners[0]].vertex];
        const Vector3 p1 = converted.positions[converted.corners[triangle.corners[1]].vertex];
        const Vector3 p2 = converted.positions[converted.corners[triangle.corners[2]].vertex];
        require(dot(cross(p1-p0, p2-p0), converted.corners[triangle.corners[0]].normal) > 0, "mirror winding/normal mismatch");
        StaticMeshImportOptions bad_options;
        bad_options.scale = 0;
        require(!import_static_meshes(files, path("/Output/axis.fbx"), bad_options).succeeded(), "zero scale accepted");
        replace_text(files, "/Output/broken.fbx", "broken");
        require(!import_static_mesh_asset(files, path("/Output/broken.fbx"), id).succeeded(), "broken FBX accepted");
        std::cout << "StaticMesh import tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
