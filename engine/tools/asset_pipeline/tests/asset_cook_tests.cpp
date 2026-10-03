#include "asset_pipeline/asset_cook.h"

#include <cstdlib>
#include <iostream>
#include "asset/asset_pair_store.h"
#include "asset/texture/texture_asset.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"

namespace
{
    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
    void mount(toy3d::FileSystem& files, toy3d::NativePlatformFile& platform, const toy3d::PhysicalPath& root)
    {
        toy3d::DirectoryFileStoreDesc descriptor;
        descriptor.physical_root = root;
        const auto store = toy3d::DirectoryFileStore::create(platform, descriptor);
        check(store.succeeded(), "Cook fixture store failed");
        toy3d::FileMountDesc entry;
        entry.virtual_root = toy3d::VirtualPath::parse("/Project").value();
        entry.store = store.value();
        entry.access = toy3d::MountAccess::ReadWrite;
        entry.allow_enumeration = true;
        check(files.add_mount(entry).succeeded() && files.freeze().succeeded(), "Cook fixture mount failed");
    }
} // namespace

int main()
{
    using namespace toy3d;
    AssetId id;
    check(AssetId::try_generate(id), "Cook fixture identity failed");
    NativePlatformFile platform;
    const PhysicalPath root(std::string(TOY3D_COOK_TEST_ROOT) + "/" + id.hex());
    const PhysicalPath source(root.utf8() + "/source");
    const PhysicalPath cooked(root.utf8() + "/cooked");
    check(platform.create_directories(source).succeeded(), "Cook fixture directory failed");
    TypeRegistry types;
    check(register_texture_asset_types(types).succeeded() && types.freeze().succeeded(), "Cook fixture types failed");
    FileSystem files;
    mount(files, platform, source);
    Texture2DAsset texture;
    texture.width = texture.height = 1u;
    texture.format = PixelFormat::R8G8B8A8UNormSRGB;
    texture.mips.push_back({4u, 4u, {20u, 40u, 60u, 255u}});
    AssetPairStore pairs(types, files);
    const auto path = VirtualPath::parse("/Project/sample.asset").value();
    const auto encoded = encode_texture_asset_pair(types, id, texture);
    check(encoded.succeeded() && pairs.publish(path, encoded.value(), FilePublishMode::CreateNew).succeeded(),
          "Cook fixture texture publication failed");
    const auto original = pairs.read(path);
    check(original.succeeded(), "Cook fixture pair read failed");
    auto segments = original.value().meta.segments;
    segments.push_back({"editor_test", 2u, false, {1u, 2u, 3u}});
    const auto with_editor_data = encode_asset_pair(types, original.value().description.index,
                                                    original.value().description.type_data, std::move(segments));
    check(with_editor_data.succeeded() &&
              pairs.publish(path, with_editor_data.value(), FilePublishMode::Replace).succeeded(),
          "Cook optional metadata fixture failed");
    const auto catalog = scan_asset_catalog(types, files, {VirtualPath::parse("/Project").value()});
    check(catalog.succeeded(), "Cook fixture catalog failed");
    check(cook_runtime_assets(types, files, catalog.value(), cooked).succeeded(), "Runtime Cook failed");
    FileSystem output;
    mount(output, platform, PhysicalPath(cooked.utf8() + "/project/asset"));
    const auto runtime_pair = read_asset_pair(types, output, path);
    const auto runtime_texture = read_texture_asset(output, path);
    check(runtime_pair.succeeded() &&
              runtime_pair.value().meta.segments.size() == original.value().meta.segments.size(),
          "Cook retained optional Editor metadata");
    check(runtime_texture.succeeded() && runtime_texture.value().mips.front().pixels == texture.mips.front().pixels,
          "Cook changed runtime texture payload");
    check(pairs.read(path).value().meta.segments.size() == original.value().meta.segments.size() + 1u,
          "Cook changed source-side metadata");
    check(!cook_runtime_assets(types, files, catalog.value(), cooked).succeeded() &&
              read_texture_asset(output, path).succeeded(),
          "Cook overwrote an existing output directory");
    AssetCatalog broken;
    auto index = original.value().description.index;
    AssetId missing;
    check(AssetId::try_generate(missing), "Missing dependency fixture identity failed");
    index.dependencies.push_back({missing, {}, "toy3d.Texture2DAssetData", AssetRefStrength::Strong});
    check(broken.index.add(path, index).succeeded(), "Missing dependency fixture failed");
    check(!cook_runtime_assets(types, files, broken, PhysicalPath(root.utf8() + "/failed")).succeeded() &&
              !platform.exists(PhysicalPath(root.utf8() + "/failed")).value(),
          "Missing strong dependency published output");
    check(files.remove_file(VirtualPath::parse("/Project/sample.meta").value()).succeeded() &&
              !cook_runtime_assets(types, files, catalog.value(), PhysicalPath(root.utf8() + "/missing-meta"))
                   .succeeded(),
          "Cook accepted a missing required payload");
    return EXIT_SUCCESS;
}
