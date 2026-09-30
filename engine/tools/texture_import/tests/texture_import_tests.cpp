#include "texture_import/texture_import.h"

#include "image_codec/png_codec.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
    }
}

int main()
{
    toy3d::Rgba8Image image;
    image.width = 2;
    image.height = 2;
    image.pixels = {0, 0, 0, 255, 255, 255, 255, 255,
                    0, 0, 0, 255, 255, 255, 255, 255};
    std::vector<std::uint8_t> png;
    check(toy3d::encode_png(image, png).succeeded(), "could not encode test PNG");
    const auto imported = toy3d::import_texture_image(png);
    check(imported.succeeded(), "PNG import failed");
    check(imported.value().mips.size() == 2u && imported.value().mips[1].pixels.size() == 4u,
        "complete mip chain was not generated");
    const auto& middle = imported.value().mips[1].pixels;
    check(middle[0] >= 186u && middle[0] <= 190u && middle[0] == middle[1] && middle[1] == middle[2] &&
        middle[3] == 255u, "sRGB mip filtering did not occur in linear space");
    toy3d::AssetId id;
    check(toy3d::AssetId::try_generate(id), "asset ID generation failed");
    const auto asset = toy3d::import_texture_asset(png, id);
    check(asset.succeeded(), "Texture2D package encoding failed");
    const auto decoded = toy3d::decode_texture_asset(asset.value());
    check(decoded.succeeded() && decoded.value().mips[1].pixels == middle,
        "Texture2D package round trip failed");
    auto truncated = asset.value();
    truncated.pop_back();
    check(!toy3d::decode_texture_asset(truncated).succeeded(), "truncated Texture2D package was accepted");
    png[0] = 0;
    check(!toy3d::import_texture_image(png).succeeded(), "invalid PNG signature was accepted");
    std::ifstream jpeg_file(TOY3D_TEXTURE_TEST_JPEG, std::ios::binary);
    check(static_cast<bool>(jpeg_file), "JPEG fixture was not found");
    const std::vector<std::uint8_t> jpeg(std::istreambuf_iterator<char>(jpeg_file), {});
    const auto jpeg_imported = toy3d::import_texture_image(jpeg);
    check(jpeg_imported.succeeded() && jpeg_imported.value().width == 2u &&
        jpeg_imported.value().mips.size() == 2u && jpeg_imported.value().mips[0].pixels[3] == 255u,
        "JPEG decode or opaque alpha conversion failed");
    return EXIT_SUCCESS;
}
