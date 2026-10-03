#include "asset_pipeline/texture_import.h"

#include "image/png_codec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace toy3d
{
    namespace
    {
        constexpr std::size_t source_limit = 32u * 1024u * 1024u;
        constexpr std::size_t job_limit = 256u * 1024u * 1024u;

        AssetStatus invalid(const std::string& message)
        {
            return {AssetErrorCode::Value, {}, {}, "texture_mips", {}, message, {}};
        }

        float srgb_to_linear(std::uint8_t encoded)
        {
            const float value = static_cast<float>(encoded) / 255.0f;
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        std::uint8_t linear_to_srgb(float value)
        {
            value = std::max(0.0f, std::min(1.0f, value));
            const float encoded = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint8_t>(std::lround(encoded * 255.0f));
        }

        TextureAssetMip downsample(const TextureAssetMip& previous, std::uint32_t source_width,
                                   std::uint32_t source_height, std::uint32_t target_width, std::uint32_t target_height,
                                   TextureUsage usage)
        {
            TextureAssetMip next;
            next.row_pitch = target_width * 4u;
            next.slice_pitch = next.row_pitch * target_height;
            next.pixels.resize(next.slice_pitch);
            std::array<float, 256> linear{};
            for (std::size_t i = 0; i < linear.size(); ++i)
            {
                linear[i] = srgb_to_linear(static_cast<std::uint8_t>(i));
            }
            for (std::uint32_t y = 0; y < target_height; ++y)
            {
                const float top = static_cast<float>(y) * source_height / target_height;
                const float bottom = static_cast<float>(y + 1u) * source_height / target_height;
                for (std::uint32_t x = 0; x < target_width; ++x)
                {
                    const float left = static_cast<float>(x) * source_width / target_width;
                    const float right = static_cast<float>(x + 1u) * source_width / target_width;
                    float accum[4]{};
                    for (std::uint32_t sy = static_cast<std::uint32_t>(top);
                         sy < static_cast<std::uint32_t>(std::ceil(bottom)); ++sy)
                    {
                        const float height =
                            std::min(bottom, static_cast<float>(sy + 1u)) - std::max(top, static_cast<float>(sy));
                        for (std::uint32_t sx = static_cast<std::uint32_t>(left);
                             sx < static_cast<std::uint32_t>(std::ceil(right)); ++sx)
                        {
                            const float width =
                                std::min(right, static_cast<float>(sx + 1u)) - std::max(left, static_cast<float>(sx));
                            const float weight = width * height;
                            const std::size_t offset = static_cast<std::size_t>(sy) * previous.row_pitch + sx * 4u;
                            for (std::size_t channel = 0; channel < 3u; ++channel)
                            {
                                const float encoded = static_cast<float>(previous.pixels[offset + channel]) / 255.0f;
                                const float value =
                                    usage == TextureUsage::Color
                                        ? linear[previous.pixels[offset + channel]]
                                        : (usage == TextureUsage::Normal ? encoded * 2.0f - 1.0f : encoded);
                                accum[channel] += value * weight;
                            }
                            accum[3] += static_cast<float>(previous.pixels[offset + 3u]) * weight;
                        }
                    }
                    const float area = (right - left) * (bottom - top);
                    const std::size_t output = static_cast<std::size_t>(y) * next.row_pitch + x * 4u;
                    float normal_length = 1.0f;
                    if (usage == TextureUsage::Normal)
                    {
                        normal_length = std::sqrt(accum[0] * accum[0] + accum[1] * accum[1] + accum[2] * accum[2]);
                        if (normal_length < 1.0e-6f)
                        {
                            accum[0] = accum[1] = 0.0f;
                            accum[2] = normal_length = 1.0f;
                        }
                    }
                    for (std::size_t channel = 0; channel < 3u; ++channel)
                    {
                        if (usage == TextureUsage::Color)
                        {
                            next.pixels[output + channel] = linear_to_srgb(accum[channel] / area);
                        }
                        else
                        {
                            const float value = usage == TextureUsage::Normal
                                                    ? accum[channel] / normal_length * 0.5f + 0.5f
                                                    : accum[channel] / area;
                            next.pixels[output + channel] =
                                static_cast<std::uint8_t>(std::lround(std::max(0.0f, std::min(1.0f, value)) * 255.0f));
                        }
                    }
                    next.pixels[output + 3u] = static_cast<std::uint8_t>(std::lround(accum[3] / area));
                }
            }
            return next;
        }
    } // namespace

    AssetResult<Texture2DAsset> import_texture_image(const std::vector<std::uint8_t>& source,
                                                     const TextureImportSettings& settings)
    {
        if (!is_valid_texture_usage(settings.usage) || (settings.flip_green && settings.usage != TextureUsage::Normal))
        {
            return AssetResult<Texture2DAsset>(invalid("Invalid texture import usage or green-channel option."));
        }
        if (source.empty() || source.size() > source_limit)
        {
            return AssetResult<Texture2DAsset>(invalid("Source image exceeds the 32 MiB import limit."));
        }
        Rgba8Image image;
        ImageLimits limits;
        limits.max_dimension = 4096u;
        limits.max_encoded_bytes = source_limit;
        const ImageStatus decoded = decode_image(source, image, limits);
        if (!decoded.succeeded())
        {
            return AssetResult<Texture2DAsset>(invalid(decoded.message));
        }
        const std::size_t base_bytes = image.pixels.size();
        // The decoded mip chain, contiguous payload and final package may overlap.
        if (base_bytes > (job_limit - source.size()) / 4u)
        {
            return AssetResult<Texture2DAsset>(invalid("Texture import exceeds the 256 MiB job budget."));
        }
        Texture2DAsset result;
        result.width = image.width;
        result.height = image.height;
        result.usage = settings.usage;
        result.flip_green = settings.flip_green;
        result.format =
            settings.usage == TextureUsage::Color ? PixelFormat::R8G8B8A8UNormSRGB : PixelFormat::R8G8B8A8UNorm;
        if (settings.usage == TextureUsage::Normal)
        {
            for (std::size_t offset = 0u; offset < image.pixels.size(); offset += 4u)
            {
                float vector[3]{};
                for (std::size_t channel = 0u; channel < 3u; ++channel)
                {
                    vector[channel] = static_cast<float>(image.pixels[offset + channel]) / 255.0f * 2.0f - 1.0f;
                }
                if (settings.flip_green)
                {
                    vector[1] = -vector[1];
                }
                const float length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
                if (length < 1.0e-6f)
                {
                    return AssetResult<Texture2DAsset>(invalid("Normal texture contains a degenerate encoded vector."));
                }
                for (std::size_t channel = 0u; channel < 3u; ++channel)
                {
                    image.pixels[offset + channel] =
                        static_cast<std::uint8_t>(std::lround((vector[channel] / length * 0.5f + 0.5f) * 255.0f));
                }
            }
        }
        TextureAssetMip first;
        first.row_pitch = image.width * 4u;
        first.slice_pitch = first.row_pitch * image.height;
        first.pixels = std::move(image.pixels);
        result.mips.push_back(std::move(first));
        std::uint32_t width = image.width;
        std::uint32_t height = image.height;
        while (width > 1u || height > 1u)
        {
            const std::uint32_t next_width = std::max(1u, width / 2u);
            const std::uint32_t next_height = std::max(1u, height / 2u);
            result.mips.push_back(
                downsample(result.mips.back(), width, height, next_width, next_height, settings.usage));
            width = next_width;
            height = next_height;
        }
        const AssetStatus valid = validate_texture_asset(result);
        if (!valid.succeeded())
        {
            return AssetResult<Texture2DAsset>(valid);
        }
        return AssetResult<Texture2DAsset>(std::move(result));
    }

} // namespace toy3d
