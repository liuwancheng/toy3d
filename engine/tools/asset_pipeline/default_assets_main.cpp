#include "asset/asset_catalog.h"
#include "asset/asset_pair_store.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "asset/material/material_asset.h"
#include "asset/texture/texture_asset.h"
#include "asset_pipeline/texture_import.h"

#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace toy3d
{
    namespace
    {
        struct TextureSpec
        {
            const char* name;
            const char* source;
            const char* id;
        };

        constexpr std::array<TextureSpec, 3> textures = {{
            {"T_White", "T_White.png", "26e14823067241ee84676de813b2e8c3"},
            {"T_Black", "T_Black.png", "a2d7e64e85c940e583f9ac71f5c44833"},
            {"T_RedBrick", "T_RedBrick.jpg", "a6a53651e980491294a675d094006da0"}
        }};

        bool publish_texture(NativePlatformFile& platform, AssetPairStore& store,
            const TypeRegistry& types, const PhysicalPath& source_root,
            const TextureSpec& spec)
        {
            const auto source = platform.join_relative(source_root, spec.source);
            if (!source.succeeded()) { std::cerr << source.status().message << '\n'; return false; }
            const auto bytes = platform.read_binary(source.value());
            if (!bytes.succeeded()) { std::cerr << bytes.status().message << '\n'; return false; }
            const auto texture = import_texture_image(bytes.value());
            if (!texture.succeeded()) { std::cerr << texture.status().message << '\n'; return false; }
            AssetId id;
            if (!AssetId::parse(spec.id, id)) { std::cerr << "Invalid texture ID\n"; return false; }
            const auto pair = encode_texture_asset_pair(types, id, texture.value());
            if (!pair.succeeded()) { std::cerr << pair.status().message << '\n'; return false; }
            const auto path = VirtualPath::parse(std::string("/Engine/") + spec.name + ".asset");
            if (!path.succeeded()) { std::cerr << path.status().message << '\n'; return false; }
            const AssetStatus published = store.publish(path.value(), pair.value(), FilePublishMode::CreateNew);
            if (!published.succeeded()) { std::cerr << published.message << '\n'; return false; }
            std::cout << spec.name << ": " << texture.value().width << 'x' << texture.value().height << '\n';
            return true;
        }

        AssetRef texture_reference(const char* hex)
        {
            AssetRef reference;
            AssetId::parse(hex, reference.asset_id);
            reference.expected_type = "toy3d.Texture2DAssetData";
            return reference;
        }

        bool publish_material(AssetPairStore& store, const TypeRegistry& types,
            const char* name, const char* hex, const char* shader_name,
            const char* texture_parameter, const char* texture_id, const Vector4& color)
        {
            AssetId id;
            if (!AssetId::parse(hex, id)) { std::cerr << "Invalid material ID\n"; return false; }
            MaterialAssetData material;
            material.shader_name = shader_name;
            MaterialParameterOverride color_override;
            color_override.name = "base_color";
            color_override.value = color;
            material.overrides.push_back(color_override);
            MaterialParameterOverride texture_override;
            texture_override.name = texture_parameter;
            texture_override.value = texture_reference(texture_id);
            material.overrides.push_back(texture_override);
            const auto pair = encode_material_asset_pair(types, id, material);
            if (!pair.succeeded()) { std::cerr << pair.status().message << '\n'; return false; }
            const auto path = VirtualPath::parse(std::string("/Engine/") + name + ".asset");
            if (!path.succeeded()) { std::cerr << path.status().message << '\n'; return false; }
            const AssetStatus published = store.publish(path.value(), pair.value(), FilePublishMode::CreateNew);
            if (!published.succeeded()) { std::cerr << published.message << '\n'; return false; }
            std::cout << name << '\n';
            return true;
        }
    } // namespace
} // namespace toy3d

int main(int argc, char** argv)
{
    using namespace toy3d;
    if (argc != 3)
    {
        std::cerr << "Usage: Toy3dDefaultAssets <source-directory> <empty-output-directory>\n";
        return 1;
    }
    NativePlatformFile platform;
    const PhysicalPath source_root(argv[1]);
    const PhysicalPath output_root(argv[2]);
    const FileStatus created = platform.create_directories(output_root);
    if (!created.succeeded()) { std::cerr << created.message << '\n'; return 1; }
    const auto existing = platform.enumerate_directory(output_root);
    if (!existing.succeeded() || !existing.value().empty())
    {
        std::cerr << "Output directory must be empty\n";
        return 1;
    }
    DirectoryFileStoreDesc descriptor;
    descriptor.physical_root = output_root;
    const auto directory = DirectoryFileStore::create(platform, descriptor);
    if (!directory.succeeded()) { std::cerr << directory.status().message << '\n'; return 1; }
    const auto root = VirtualPath::parse("/Engine");
    if (!root.succeeded()) { std::cerr << root.status().message << '\n'; return 1; }
    FileMountDesc mount;
    mount.virtual_root = root.value();
    mount.store = directory.value();
    mount.access = MountAccess::ReadWrite;
    mount.allow_enumeration = true;
    FileSystem files;
    const FileStatus mounted = files.add_mount(mount);
    if (!mounted.succeeded()) { std::cerr << mounted.message << '\n'; return 1; }
    const FileStatus frozen_files = files.freeze();
    if (!frozen_files.succeeded()) { std::cerr << frozen_files.message << '\n'; return 1; }
    TypeRegistry types;
    ReflectionStatus registered = register_texture_asset_types(types);
    if (registered.succeeded()) registered = register_material_asset_types(types);
    if (registered.succeeded()) registered = types.freeze();
    if (!registered.succeeded()) { std::cerr << registered.message << '\n'; return 1; }
    AssetPairStore store(types, files);
    for (const TextureSpec& texture : textures)
        if (!publish_texture(platform, store, types, source_root, texture)) return 1;
    const Vector4 white(1.0f, 1.0f, 1.0f, 1.0f);
    const Vector4 black(0.0f, 0.0f, 0.0f, 1.0f);
    if (!publish_material(store, types, "M_Default", "a82316fbd4a349b8b2dfde23697139fe",
            "Toy3d/Surface/Phong", "surface_tint_texture", textures[0].id, white) ||
        !publish_material(store, types, "M_White", "7bd54976ce62477a949839d57c39b5a4",
            "Toy3d/Surface/Unlit", "base_color_texture", textures[0].id, white) ||
        !publish_material(store, types, "M_Black", "d736e0dfa85f468392acc08840331c73",
            "Toy3d/Surface/Unlit", "base_color_texture", textures[1].id, black) ||
        !publish_material(store, types, "M_Brick", "096dc165e01c4cfdbf5c93e4b831f41f",
            "Toy3d/Surface/Phong", "surface_tint_texture", textures[2].id, white)) return 1;
    const auto catalog = scan_asset_catalog(types, files, {root.value()});
    if (!catalog.succeeded() || catalog.value().entries.size() != textures.size() + 4u)
    {
        std::cerr << (catalog.succeeded() ? "Incomplete default asset catalog" : catalog.status().message) << '\n';
        return 1;
    }
    std::cout << "Validated " << catalog.value().entries.size() << " engine assets\n";
    return 0;
}
