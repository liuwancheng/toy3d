#include "assets/texture/texture_preview_image.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#include "asset/thumbnail/asset_thumbnail.h"

int main()
{
    using namespace toy3d;
    Texture2DAsset texture;
    texture.width = 2;
    texture.height = 1;
    texture.format = PixelFormat::R8G8B8A8UNormSRGB;
    texture.mips.push_back({8, 8, {10, 20, 30, 40, 50, 60, 70, 80}});
    texture.mips.push_back({4, 4, {1, 2, 3, 4}});
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
    std::string error;
    const auto check = [&](TexturePreviewChannel channel, std::uint32_t mip, const std::vector<std::uint8_t>& expected)
    {
        return make_texture_preview_pixels(texture, mip, channel, width, height, pixels, error) && pixels == expected &&
               width == (mip ? 1u : 2u) && height == 1u && error.empty();
    };
    if (!check(TexturePreviewChannel::RGBA, 0, {30, 20, 10, 40, 70, 60, 50, 80}) ||
        !check(TexturePreviewChannel::Red, 0, {10, 10, 10, 255, 50, 50, 50, 255}) ||
        !check(TexturePreviewChannel::Green, 0, {20, 20, 20, 255, 60, 60, 60, 255}) ||
        !check(TexturePreviewChannel::Blue, 0, {30, 30, 30, 255, 70, 70, 70, 255}) ||
        !check(TexturePreviewChannel::Alpha, 0, {40, 40, 40, 255, 80, 80, 80, 255}) ||
        !check(TexturePreviewChannel::RGBA, 1, {3, 2, 1, 4}))
    {
        std::cerr << "Texture preview channel or mip conversion failed: " << error;
        return EXIT_FAILURE;
    }
    const auto previous = pixels;
    texture.mips[0].row_pitch = 7;
    if (make_texture_preview_pixels(texture, 0, TexturePreviewChannel::RGBA, width, height, pixels, error) ||
        pixels != previous)
    {
        std::cerr << "Invalid row pitch changed the preview image.";
        return EXIT_FAILURE;
    }
    texture.width = 256;
    texture.height = 128;
    texture.mips.clear();
    texture.mips.push_back({1024, 131072, std::vector<std::uint8_t>(131072, 255)});
    TextureAssetMip small;
    small.row_pitch = 512;
    small.slice_pitch = 32768;
    small.pixels.resize(small.slice_pitch);
    for (std::size_t i = 0; i < small.pixels.size(); i += 4)
    {
        small.pixels[i] = 10;
        small.pixels[i + 1] = 20;
        small.pixels[i + 2] = 30;
        small.pixels[i + 3] = 255;
    }
    texture.mips.push_back(small);
    if (!make_texture_thumbnail_pixels(texture, pixels, error) || !error.empty() ||
        pixels.size() != static_cast<std::size_t>(thumbnail_default_size) * thumbnail_default_size * 4u)
    {
        std::cerr << "Thumbnail mip selection failed: " << error;
        return EXIT_FAILURE;
    }
    const std::size_t center = (64u * thumbnail_default_size + 64u) * 4u;
    if (pixels[center] != 30u || pixels[center + 1u] != 20u || pixels[center + 2u] != 10u ||
        pixels[center + 3u] != 255u || pixels[0] != 180u)
    {
        std::cerr << "Thumbnail did not preserve mip color, opaque alpha or aspect padding.";
        return EXIT_FAILURE;
    }
    for (std::size_t i = 3; i < texture.mips[1].pixels.size(); i += 4)
    {
        texture.mips[1].pixels[i] = 0;
    }
    if (!make_texture_thumbnail_pixels(texture, pixels, error) || pixels[center] != 180u ||
        pixels[center + 1u] != 180u || pixels[center + 2u] != 180u || pixels[center + 3u] != 255u)
    {
        std::cerr << "Transparent thumbnail pixels did not use the checkerboard.";
        return EXIT_FAILURE;
    }
    const auto thumbnail = pixels;
    texture.mips[1].row_pitch = 1;
    if (make_texture_thumbnail_pixels(texture, pixels, error) || pixels != thumbnail)
    {
        std::cerr << "Invalid thumbnail rows replaced the prior output.";
        return EXIT_FAILURE;
    }
    std::cout << "Texture preview channels, mip selection and invalid pitch passed.\n";
    return EXIT_SUCCESS;
}
