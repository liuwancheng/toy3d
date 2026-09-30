#include "panels/texture_preview_image.h"

#include <cstdlib>
#include <iostream>
#include <vector>

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
    const auto check = [&](TexturePreviewChannel channel, std::uint32_t mip,
                           const std::vector<std::uint8_t>& expected)
    {
        return make_texture_preview_pixels(texture, mip, channel, width, height, pixels, error) &&
            pixels == expected && width == (mip ? 1u : 2u) && height == 1u && error.empty();
    };
    if (!check(TexturePreviewChannel::RGBA, 0, {30, 20, 10, 40, 70, 60, 50, 80}) ||
        !check(TexturePreviewChannel::Red, 0, {10, 10, 10, 255, 50, 50, 50, 255}) ||
        !check(TexturePreviewChannel::Green, 0, {20, 20, 20, 255, 60, 60, 60, 255}) ||
        !check(TexturePreviewChannel::Blue, 0, {30, 30, 30, 255, 70, 70, 70, 255}) ||
        !check(TexturePreviewChannel::Alpha, 0, {40, 40, 40, 255, 80, 80, 80, 255}) ||
        !check(TexturePreviewChannel::RGBA, 1, {3, 2, 1, 4}))
    { std::cerr << "Texture preview channel or mip conversion failed: " << error; return EXIT_FAILURE; }
    const auto previous = pixels;
    texture.mips[0].row_pitch = 7;
    if (make_texture_preview_pixels(texture, 0, TexturePreviewChannel::RGBA,
                                    width, height, pixels, error) || pixels != previous)
    { std::cerr << "Invalid row pitch changed the preview image."; return EXIT_FAILURE; }
    std::cout << "Texture preview channels, mip selection and invalid pitch passed.\n";
    return EXIT_SUCCESS;
}
