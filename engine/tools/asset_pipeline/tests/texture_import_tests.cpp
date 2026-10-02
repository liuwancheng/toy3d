#include "asset_pipeline/texture_import.h"

#include "image/png_codec.h"
#include "asset/asset_pair_store.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
} // namespace

int main()
{
    toy3d::Rgba8Image image;
    image.width = 2;
    image.height = 2;
    image.pixels = {0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    std::vector<std::uint8_t> png;
    check(toy3d::encode_png(image, png).succeeded(), "could not encode test PNG");
    const auto imported = toy3d::import_texture_image(png);
    check(imported.succeeded(), "PNG import failed");
    check(imported.value().mips.size() == 2u && imported.value().mips[1].pixels.size() == 4u,
          "complete mip chain was not generated");
    const auto& middle = imported.value().mips[1].pixels;
    check(middle[0] >= 186u && middle[0] <= 190u && middle[0] == middle[1] && middle[1] == middle[2] &&
              middle[3] == 255u,
          "sRGB mip filtering did not occur in linear space");
    toy3d::AssetId id;
    check(toy3d::AssetId::try_generate(id), "asset ID generation failed");
    const auto asset = toy3d::encode_texture_asset(id, imported.value());
    check(asset.succeeded(), "Texture2D package encoding failed");
    const auto decoded = toy3d::decode_texture_asset(asset.value());
    check(decoded.succeeded() && decoded.value().mips[1].pixels == middle, "Texture2D package round trip failed");
    toy3d::TypeRegistry types;
    check(toy3d::register_texture_asset_types(types).succeeded() && types.freeze().succeeded(),
          "Texture2D pair schema registration failed");
    const auto pair = toy3d::encode_texture_asset_pair(types, id, imported.value());
    check(pair.succeeded() && pair.value().has_meta, "Texture2D pair encoding failed");
    toy3d::NativePlatformFile platform;
    const auto root = platform.join_relative(toy3d::PhysicalPath(TOY3D_TEXTURE_PAIR_TEST_ROOT), id.hex());
    check(root.succeeded() && platform.create_directories(root.value()).succeeded(),
          "Texture2D pair fixture directory failed");
    toy3d::DirectoryFileStoreDesc descriptor;
    descriptor.physical_root = root.value();
    const auto store = toy3d::DirectoryFileStore::create(platform, descriptor);
    check(store.succeeded(), "Texture2D pair store failed");
    toy3d::FileSystem files;
    toy3d::FileMountDesc mount;
    mount.virtual_root = toy3d::VirtualPath::parse("/Asset").value();
    mount.store = store.value();
    mount.access = toy3d::MountAccess::ReadWrite;
    check(files.add_mount(mount).succeeded() && files.freeze().succeeded(), "Texture2D pair mount failed");
    toy3d::AssetPairStore assets(types, files);
    const auto path = toy3d::VirtualPath::parse("/Asset/check.asset").value();
    const auto meta = toy3d::VirtualPath::parse("/Asset/check.meta").value();
    check(assets.publish(path, pair.value(), toy3d::FilePublishMode::CreateNew).succeeded(),
          "Texture2D pair publication failed");
    const auto paired_texture = toy3d::read_texture_asset(files, path);
    check(paired_texture.succeeded() && paired_texture.value().mips[1].pixels == middle,
          "Texture2D pair runtime read failed");
    check(files.remove_file(meta).succeeded() && !toy3d::read_texture_asset(files, path).succeeded(),
          "Texture2D missing meta was accepted");
    auto truncated = asset.value();
    truncated.pop_back();
    check(!toy3d::decode_texture_asset(truncated).succeeded(), "truncated Texture2D package was accepted");
    png[0] = 0;
    check(!toy3d::import_texture_image(png).succeeded(), "invalid PNG signature was accepted");
    std::ifstream jpeg_file(TOY3D_TEXTURE_TEST_JPEG, std::ios::binary);
    check(static_cast<bool>(jpeg_file), "JPEG fixture was not found");
    const std::vector<std::uint8_t> jpeg(std::istreambuf_iterator<char>(jpeg_file), {});
    const auto jpeg_imported = toy3d::import_texture_image(jpeg);
    check(jpeg_imported.succeeded() && jpeg_imported.value().width == 2u && jpeg_imported.value().mips.size() == 2u &&
              jpeg_imported.value().mips[0].pixels[3] == 255u,
          "JPEG decode or opaque alpha conversion failed");
    return EXIT_SUCCESS;
}
