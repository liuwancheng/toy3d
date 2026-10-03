#include "asset_pipeline/mesh_tangents.h"
#include "asset_pipeline/skeletal_mesh_builder.h"

#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"

#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }
} // namespace

int main()
{
    using namespace toy3d;
    NativePlatformFile platform;
    DirectoryFileStoreDesc descriptor;
    descriptor.physical_root = PhysicalPath(TOY3D_TANGENT_CONTROLLED_ASSET_ROOT);
    const auto store = DirectoryFileStore::create(platform, descriptor);
    check(store.succeeded(), "Controlled asset read-only store failed");
    if (!store.succeeded())
    {
        return 1;
    }
    FileSystem files;
    FileMountDesc mount;
    mount.virtual_root = VirtualPath::parse("/Project").value();
    mount.store = store.value();
    mount.access = MountAccess::ReadOnly;
    check(files.add_mount(mount).succeeded() && files.freeze().succeeded(), "Controlled asset mount failed");
    const auto controlled =
        read_static_mesh_asset(files, VirtualPath::parse("/Project/cubes_with_names.asset").value());
    check(controlled.succeeded(), "Current strict asset reader must load the offline rebuilt controlled mesh");
    if (controlled.succeeded())
    {
        check(controlled.value().valid_tangent_frame && controlled.value().vertices.size() == 1260u &&
                  controlled.value().indices.size() == 1260u &&
                  controlled.value().material_slots == std::vector<std::string>{"Mat_Green_0", "Mat_Red_1"},
              "Controlled mesh preserves source vertices, indices and material slots in the current tangent format");
    }
    StaticMeshAssetGeometry source;
    source.material_slots = {"surface"};
    source.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}, {255, 20, 30, 40}},
                       {{1, 0, 0}, {0, 0, 1}, {1, 0}},
                       {{0, 1, 0}, {0, 0, 1}, {0, 1}},
                       {{0, -1, 0}, {0, 0, 1}, {0, 1}}};
    source.indices = {0, 1, 2, 0, 3, 1};
    source.sections = {{0, 6, 0}};
    const auto tangents = build_mesh_tangents(source);
    check(tangents.succeeded(), "MikkTSpace mirrored UV generation failed");
    if (!tangents.succeeded())
    {
        std::cerr << tangents.status().message << '\n';
        return 1;
    }
    const auto& geometry = tangents.value().geometry;
    check(geometry.valid_tangent_frame && geometry.vertices.size() > source.vertices.size(),
          "Mirrored UV must split shared vertices while retaining valid tangent frames");
    const auto& first = geometry.vertices[geometry.indices[0]];
    const auto& mirrored = geometry.vertices[geometry.indices[3]];
    check(first.tangent.w == -mirrored.tangent.w && first.color == mirrored.color &&
              tangents.value().source_vertices[geometry.indices[0]] == 0u &&
              tangents.value().source_vertices[geometry.indices[3]] == 0u,
          "Corner split must preserve attributes and return original vertex mapping");
    const auto encoded = encode_static_mesh_geometry(geometry);
    check(encoded.succeeded(), "Tangent payload encoding failed");
    const auto decoded = decode_static_mesh_geometry(encoded.value());
    check(decoded.succeeded() && decoded.value().valid_tangent_frame &&
              decoded.value().vertices[decoded.value().indices[3]].tangent.w == mirrored.tangent.w,
          "Geometry payload must persist tangent sign and valid-frame capability");
    auto damaged = geometry;
    damaged.vertices[0].tangent = {0, 0, 1, 1};
    check(!validate_static_mesh_geometry(damaged).succeeded(), "Declared tangent frame must be orthogonal");
    for (auto& vertex : source.vertices)
    {
        vertex.uv0 = {};
    }
    const auto no_uv = build_mesh_tangents(source);
    check(no_uv.succeeded() && !no_uv.value().geometry.valid_tangent_frame &&
              validate_static_mesh_geometry(no_uv.value().geometry).succeeded(),
          "Degenerate UV keeps an explicit invalid-frame capability and finite fallback data");

    SkeletalMeshBuildInput skin;
    skin.mesh = tangents.value().geometry;
    skin.mesh.valid_tangent_frame = false;
    // Recreate the source geometry so the skeletal builder must perform its own split.
    skin.mesh.vertices.resize(4u);
    skin.mesh.indices = {0, 1, 2, 0, 3, 1};
    skin.influences = {{{0, 0.7f}, {1, 0.3f}}, {{0, 1.0f}}, {{0, 1.0f}}, {{1, 1.0f}}};
    skin.inverse_bind_matrices = {Matrix4{}, Matrix4{}};
    SkeletonAssetData skeleton;
    skeleton.bones = {{"root", -1, {}}, {"child", 0, {}}};
    AssetId skeleton_id;
    check(AssetId::try_generate(skeleton_id), "Skeleton ID generation failed");
    const auto built = build_skeletal_mesh(skin, skeleton_id, skeleton);
    check(built.succeeded(), "Skeletal MikkTSpace build failed");
    if (built.succeeded())
    {
        const auto& output = built.value().mesh.geometry;
        const auto a = output.mesh.indices[0];
        const auto b = output.mesh.indices[3];
        check(a != b && output.skin_weights[a].weights == output.skin_weights[b].weights &&
                  output.skin_weights[a].bone_indices == output.skin_weights[b].bone_indices &&
                  output.mesh.vertices[a].color == output.mesh.vertices[b].color,
              "Skeletal tangent split must preserve quantized influences and all vertex attributes");
    }
    else
    {
        std::cerr << built.status().message << '\n';
    }
    return failures == 0 ? 0 : 1;
}
