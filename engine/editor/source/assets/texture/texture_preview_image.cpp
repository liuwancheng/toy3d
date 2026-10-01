#include "assets/texture/texture_preview_image.h"

#include <algorithm>
#include <utility>

namespace toy3d
{
    bool make_texture_preview_pixels(const Texture2DAsset& asset, std::uint32_t mip,
        TexturePreviewChannel channel, std::uint32_t& width, std::uint32_t& height,
        std::vector<std::uint8_t>& bgra, std::string& error)
    {
        if (!asset.width || !asset.height || mip >= asset.mips.size() || mip >= 32u ||
            asset.format != PixelFormat::R8G8B8A8UNormSRGB)
        { error = "Unsupported texture mip or format."; return false; }
        const std::uint32_t mip_width = std::max(1u, asset.width >> mip);
        const std::uint32_t mip_height = std::max(1u, asset.height >> mip);
        const TextureAssetMip& source = asset.mips[mip];
        const std::uint64_t row_bytes = static_cast<std::uint64_t>(mip_width) * 4u;
        if (!mip_width || !mip_height || row_bytes > source.row_pitch ||
            static_cast<std::uint64_t>(source.row_pitch) * mip_height > source.pixels.size())
        { error = "Texture mip rows are invalid."; return false; }
        std::vector<std::uint8_t> candidate(static_cast<std::size_t>(row_bytes) * mip_height);
        for (std::uint32_t y = 0; y < mip_height; ++y)
            for (std::uint32_t x = 0; x < mip_width; ++x)
            {
                const std::size_t src = static_cast<std::size_t>(y) * source.row_pitch + x * 4u;
                const std::size_t dst = (static_cast<std::size_t>(y) * mip_width + x) * 4u;
                if (channel == TexturePreviewChannel::RGBA)
                {
                    candidate[dst] = source.pixels[src + 2];
                    candidate[dst + 1] = source.pixels[src + 1];
                    candidate[dst + 2] = source.pixels[src];
                    candidate[dst + 3] = source.pixels[src + 3];
                }
                else
                {
                    const std::size_t component = channel == TexturePreviewChannel::Red ? 0u :
                        channel == TexturePreviewChannel::Green ? 1u :
                        channel == TexturePreviewChannel::Blue ? 2u : 3u;
                    const std::uint8_t value = source.pixels[src + component];
                    candidate[dst] = candidate[dst + 1] = candidate[dst + 2] = value;
                    candidate[dst + 3] = 255u;
                }
            }
        width = mip_width;
        height = mip_height;
        bgra = std::move(candidate);
        error.clear();
        return true;
    }
}
