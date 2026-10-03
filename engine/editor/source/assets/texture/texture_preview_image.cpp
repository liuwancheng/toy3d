#include "assets/texture/texture_preview_image.h"

#include <algorithm>
#include <utility>

#include "asset/thumbnail/asset_thumbnail.h"

namespace toy3d
{
    bool make_texture_preview_pixels(const Texture2DAsset& asset, std::uint32_t mip, TexturePreviewChannel channel,
                                     std::uint32_t& width, std::uint32_t& height, std::vector<std::uint8_t>& bgra,
                                     std::string& error)
    {
        if (!asset.width || !asset.height || mip >= asset.mips.size() || mip >= 32u ||
            !texture_usage_matches_format(asset.usage, asset.format))
        {
            error = "Unsupported texture mip or format.";
            return false;
        }
        const std::uint32_t mip_width = std::max(1u, asset.width >> mip);
        const std::uint32_t mip_height = std::max(1u, asset.height >> mip);
        const TextureAssetMip& source = asset.mips[mip];
        const std::uint64_t row_bytes = static_cast<std::uint64_t>(mip_width) * 4u;
        if (!mip_width || !mip_height || row_bytes > source.row_pitch ||
            static_cast<std::uint64_t>(source.row_pitch) * mip_height > source.pixels.size())
        {
            error = "Texture mip rows are invalid.";
            return false;
        }
        std::vector<std::uint8_t> candidate(static_cast<std::size_t>(row_bytes) * mip_height);
        for (std::uint32_t y = 0; y < mip_height; ++y)
        {
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
                    const std::size_t component = channel == TexturePreviewChannel::Red     ? 0u
                                                  : channel == TexturePreviewChannel::Green ? 1u
                                                  : channel == TexturePreviewChannel::Blue  ? 2u
                                                                                            : 3u;
                    const std::uint8_t value = source.pixels[src + component];
                    candidate[dst] = candidate[dst + 1] = candidate[dst + 2] = value;
                    candidate[dst + 3] = 255u;
                }
            }
        }
        width = mip_width;
        height = mip_height;
        bgra = std::move(candidate);
        error.clear();
        return true;
    }
    bool make_texture_thumbnail_pixels(const Texture2DAsset& asset, std::vector<std::uint8_t>& bgra, std::string& error)
    {
        if (!asset.width || !asset.height ||
            (asset.format != PixelFormat::R8G8B8A8UNormSRGB && asset.format != PixelFormat::R8G8B8A8UNorm) ||
            asset.mips.empty())
        {
            error = "Texture2D thumbnail mip is invalid.";
            return false;
        }
        std::uint32_t level = 0;
        std::uint32_t width = asset.width;
        std::uint32_t height = asset.height;
        while (level + 1u < asset.mips.size() && (width > thumbnail_default_size || height > thumbnail_default_size))
        {
            ++level;
            width = std::max(1u, width / 2u);
            height = std::max(1u, height / 2u);
        }
        const TextureAssetMip& source = asset.mips[level];
        if (source.row_pitch < width * 4u ||
            static_cast<std::uint64_t>(source.row_pitch) * height > source.pixels.size())
        {
            error = "Texture2D thumbnail mip is invalid.";
            return false;
        }
        constexpr std::uint32_t size = thumbnail_default_size;
        std::vector<std::uint8_t> candidate;
        candidate.resize(static_cast<std::size_t>(size) * size * 4u);
        const float fit = std::min(static_cast<float>(size) / asset.width, static_cast<float>(size) / asset.height);
        const std::uint32_t drawn_width = std::max(1u, static_cast<std::uint32_t>(asset.width * fit));
        const std::uint32_t drawn_height = std::max(1u, static_cast<std::uint32_t>(asset.height * fit));
        const std::uint32_t left = (size - drawn_width) / 2u;
        const std::uint32_t top = (size - drawn_height) / 2u;
        for (std::uint32_t y = 0; y < size; ++y)
        {
            for (std::uint32_t x = 0; x < size; ++x)
            {
                const std::size_t dst = (static_cast<std::size_t>(y) * size + x) * 4u;
                const std::uint8_t background = ((x / 8u + y / 8u) & 1u) ? 100u : 180u;
                candidate[dst] = candidate[dst + 1] = candidate[dst + 2] = background;
                candidate[dst + 3] = 255u;
                if (x < left || y < top || x >= left + drawn_width || y >= top + drawn_height)
                {
                    continue;
                }
                const std::uint32_t sx = std::min(width - 1u, (x - left) * width / drawn_width);
                const std::uint32_t sy = std::min(height - 1u, (y - top) * height / drawn_height);
                const std::size_t src = static_cast<std::size_t>(sy) * source.row_pitch + sx * 4u;
                const std::uint32_t alpha = source.pixels[src + 3];
                for (std::uint32_t component = 0; component < 3u; ++component)
                {
                    const std::uint8_t color = source.pixels[src + 2u - component];
                    candidate[dst + component] =
                        static_cast<std::uint8_t>((color * alpha + background * (255u - alpha)) / 255u);
                }
            }
        }
        bgra = std::move(candidate);
        error.clear();
        return true;
    }
} // namespace toy3d
