#include "asset_import/static_mesh_import.h"

#include <iostream>
#include <string>

#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "static_mesh/static_mesh_asset.h"

namespace
{
    bool mount_directory(toy3d::NativePlatformFile& platform, toy3d::FileSystem& files,
                         const toy3d::PhysicalPath& physical, const char* root, bool writable)
    {
        toy3d::DirectoryFileStoreDesc desc;
        desc.physical_root = physical;
        desc.writable = writable;
        const auto store = toy3d::DirectoryFileStore::create(platform, desc);
        const auto path = toy3d::VirtualPath::parse(root);
        if (!store.succeeded()) { std::cerr << store.status().message << '\n'; return false; }
        if (!path.succeeded()) { std::cerr << path.status().message << '\n'; return false; }
        toy3d::FileMountDesc mount;
        mount.virtual_root = path.value();
        mount.store = store.value();
        mount.access = writable ? toy3d::MountAccess::ReadWrite : toy3d::MountAccess::ReadOnly;
        const auto status = files.add_mount(mount);
        if (!status.succeeded()) std::cerr << status.message << '\n';
        return status.succeeded();
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: Toy3dModelImport <source.fbx|obj|gltf|glb> <new-file.asset>\n";
        return 2;
    }
    toy3d::NativePlatformFile platform;
    const auto input = platform.canonical(toy3d::PhysicalPath(argv[1]));
    const auto output = platform.absolute(toy3d::PhysicalPath(argv[2]));
    if (!input.succeeded() || !output.succeeded()) { std::cerr << "Invalid input or output path.\n"; return 1; }
    const auto input_parent = platform.parent_path(input.value());
    const auto output_parent = platform.parent_path(output.value());
    if (!input_parent.succeeded() || !output_parent.succeeded()) { std::cerr << "Invalid parent directory.\n"; return 1; }
    const std::string source_name = input.value().utf8().substr(input.value().utf8().find_last_of("/\\") + 1);
    const std::string output_name = output.value().utf8().substr(output.value().utf8().find_last_of("/\\") + 1);
    if (output_name.size() < 6 || output_name.substr(output_name.size() - 6) != ".asset")
    { std::cerr << "Output must end with .asset.\n"; return 1; }
    const auto source = toy3d::VirtualPath::parse("/Source/" + source_name);
    const auto destination = toy3d::VirtualPath::parse("/Output/" + output_name);
    if (!source.succeeded() || !destination.succeeded()) { std::cerr << "Invalid file name.\n"; return 1; }
    toy3d::FileSystem files;
    if (!mount_directory(platform, files, input_parent.value(), "/Source", false) ||
        !mount_directory(platform, files, output_parent.value(), "/Output", true)) return 1;
    const auto frozen = files.freeze();
    if (!frozen.succeeded()) { std::cerr << frozen.message << '\n'; return 1; }
    toy3d::AssetId id;
    if (!toy3d::AssetId::try_generate(id)) { std::cerr << "Asset ID generation failed.\n"; return 1; }
    const auto imported = toy3d::import_static_mesh_asset(files, source.value(), id);
    if (!imported.succeeded()) { std::cerr << imported.status().message << '\n'; return 1; }
    const auto published = files.write_binary_atomic(destination.value(), imported.value().bytes, toy3d::FilePublishMode::CreateNew);
    if (!published.succeeded()) { std::cerr << published.message << '\n'; return 1; }
    for (const std::string& warning : imported.value().warnings) std::cerr << "Warning: " << warning << '\n';
    const auto loaded = toy3d::read_static_mesh_asset(files, destination.value());
    if (!loaded.succeeded()) { std::cerr << loaded.status().message << '\n'; return 1; }
    std::cout << "Created " << output.value().utf8() << " id=" << id.hex()
              << " vertices=" << loaded.value().vertices.size() << " indices=" << loaded.value().indices.size() << '\n';
    return 0;
}
