#pragma once

#include "image/pixel_format.h"

namespace toy3d
{
    // Persisted sampled-image meaning shared by Cook, Shader requirements and runtime.
    enum class TextureUsage : std::uint32_t
    {
        Color = 1,
        LinearData = 2,
        Normal = 3
    };

    inline bool is_valid_texture_usage(TextureUsage usage)
    {
        return usage == TextureUsage::Color || usage == TextureUsage::LinearData || usage == TextureUsage::Normal;
    }

    inline bool texture_usage_matches_format(TextureUsage usage, PixelFormat format)
    {
        return is_valid_texture_usage(usage) && (usage == TextureUsage::Color ? format == PixelFormat::R8G8B8A8UNormSRGB
                                                                              : format == PixelFormat::R8G8B8A8UNorm);
    }
} // namespace toy3d
