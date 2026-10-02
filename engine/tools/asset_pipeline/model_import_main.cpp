#include "asset_pipeline/static_mesh_import.h"

#include <iostream>
#include <string>
#include <cmath>
#include <cstdlib>

#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "math/length_units.h"
#include "asset/mesh/static_mesh_asset.h"
#include "asset/asset_pair_store.h"

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
        if (!store.succeeded())
        {
            std::cerr << store.status().message << '\n';
            return false;
        }
        if (!path.succeeded())
        {
            std::cerr << path.status().message << '\n';
            return false;
        }
        toy3d::FileMountDesc mount;
        mount.virtual_root = path.value();
        mount.store = store.value();
        mount.access = writable ? toy3d::MountAccess::ReadWrite : toy3d::MountAccess::ReadOnly;
        const auto status = files.add_mount(mount);
        if (!status.succeeded())
        {
            std::cerr << status.message << '\n';
        }
        return status.succeeded();
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: Toy3dModelImport <source.fbx|obj|gltf|glb> <new-file.asset> "
                     "[--scale N] [--source-unit-cm N] [--no-convert-scene-unit] [--ignore-file-unit]\n"
                     "Defaults: FBX file units; OBJ/glTF/GLB 100 cm per source unit.\n";
        return 2;
    }
    toy3d::StaticMeshImportOptions options;
    std::string source_extension(argv[1]);
    const auto dot = source_extension.find_last_of('.');
    source_extension = dot == std::string::npos ? "" : source_extension.substr(dot);
    for (char& c : source_extension)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    // Like the Editor, the CLI supplies format suggestions to the importer.
    options.use_file_unit = source_extension == ".fbx";
    options.source_unit_in_centimeters = options.use_file_unit ? 1.0f : toy3d::k_centimeters_per_meter;
    for (int i = 3; i < argc; ++i)
    {
        const std::string argument(argv[i]);
        if (argument == "--no-convert-scene-unit")
        {
            options.convert_scene_unit = false;
        }
        else if (argument == "--ignore-file-unit")
        {
            options.use_file_unit = false;
        }
        else if ((argument == "--scale" || argument == "--source-unit-cm") && i + 1 < argc)
        {
            char* end = nullptr;
            const float value = std::strtof(argv[++i], &end);
            if (!end || end == argv[i] || *end != '\0' || !std::isfinite(value) || value <= 0)
            {
                std::cerr << "Scale and source unit must be finite and greater than zero.\n";
                return 2;
            }
            if (argument == "--scale")
            {
                options.import_uniform_scale = value;
            }
            else
            {
                options.source_unit_in_centimeters = value;
                options.use_file_unit = false;
            }
        }
        else
        {
            std::cerr << "Unknown option or missing value: " << argument << '\n';
            return 2;
        }
    }
    toy3d::NativePlatformFile platform;
    const auto input = platform.canonical(toy3d::PhysicalPath(argv[1]));
    const auto output = platform.absolute(toy3d::PhysicalPath(argv[2]));
    if (!input.succeeded() || !output.succeeded())
    {
        std::cerr << "Invalid input or output path.\n";
        return 1;
    }
    const auto input_parent = platform.parent_path(input.value());
    const auto output_parent = platform.parent_path(output.value());
    if (!input_parent.succeeded() || !output_parent.succeeded())
    {
        std::cerr << "Invalid parent directory.\n";
        return 1;
    }
    const std::string source_name = input.value().utf8().substr(input.value().utf8().find_last_of("/\\") + 1);
    const std::string output_name = output.value().utf8().substr(output.value().utf8().find_last_of("/\\") + 1);
    if (output_name.size() < 6 || output_name.substr(output_name.size() - 6) != ".asset")
    {
        std::cerr << "Output must end with .asset.\n";
        return 1;
    }
    const auto source = toy3d::VirtualPath::parse("/Source/" + source_name);
    const auto destination = toy3d::VirtualPath::parse("/Output/" + output_name);
    if (!source.succeeded() || !destination.succeeded())
    {
        std::cerr << "Invalid file name.\n";
        return 1;
    }
    toy3d::FileSystem files;
    if (!mount_directory(platform, files, input_parent.value(), "/Source", false) ||
        !mount_directory(platform, files, output_parent.value(), "/Output", true))
    {
        return 1;
    }
    const auto frozen = files.freeze();
    if (!frozen.succeeded())
    {
        std::cerr << frozen.message << '\n';
        return 1;
    }
    toy3d::AssetId id;
    if (!toy3d::AssetId::try_generate(id))
    {
        std::cerr << "Asset ID generation failed.\n";
        return 1;
    }
    const auto imported = toy3d::import_static_mesh_asset(files, source.value(), id, options);
    if (!imported.succeeded())
    {
        std::cerr << imported.status().message << '\n';
        return 1;
    }
    toy3d::TypeRegistry types;
    const auto registered = toy3d::register_static_mesh_asset_types(types);
    if (!registered.succeeded() || !types.freeze().succeeded())
    {
        std::cerr << "Static mesh schema registration failed.\n";
        return 1;
    }
    toy3d::AssetPairStore assets(types, files);
    const auto published =
        assets.publish(destination.value(), imported.value().pair, toy3d::FilePublishMode::CreateNew);
    if (!published.succeeded())
    {
        std::cerr << published.message << '\n';
        return 1;
    }
    for (const std::string& warning : imported.value().warnings)
    {
        std::cerr << "Warning: " << warning << '\n';
    }
    const auto loaded = toy3d::read_static_mesh_asset(files, destination.value());
    if (!loaded.succeeded())
    {
        std::cerr << loaded.status().message << '\n';
        return 1;
    }
    std::cout << "Created " << output.value().utf8() << " id=" << id.hex()
              << " vertices=" << loaded.value().vertices.size() << " indices=" << loaded.value().indices.size() << '\n';
    return 0;
}
