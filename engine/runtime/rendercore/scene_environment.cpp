#include "rendercore/scene_interface.h"

#include <algorithm>

#include "asset/texture/environment_asset.h"

namespace toy3d
{
    bool validate_scene_environment_snapshot(const SceneEnvironmentSnapshot& snapshot, std::string& error)
    {
        error.clear();
        Quaternion rotation;
        if (!is_finite(snapshot.intensity) || snapshot.intensity < 0.0f || !try_normalize(snapshot.rotation, rotation))
        {
            error = "Environment requires finite nonnegative intensity and valid rotation";
            return false;
        }
        if (!snapshot.cube)
        {
            return true;
        }
        const auto& desc = snapshot.cube->desc();
        if (!desc.validate(error) || !desc.cube || desc.format != PixelFormat::R16G16B16A16Float ||
            desc.usage != TextureUsage::LinearData || !desc.requires_linear_filter || desc.width < 2u ||
            desc.width > maximum_environment_face_size || (desc.width & (desc.width - 1u)) != 0u ||
            desc.mip_pixels.size() != environment_mip_count(desc.width))
        {
            error =
                "Environment requires bounded power-of-two linear RGBA16F Cube with complete mips and linear filtering";
            return false;
        }
        for (std::size_t mip = 0u; mip < desc.mip_pixels.size(); ++mip)
        {
            const auto size = std::max(1u, desc.width >> static_cast<std::uint32_t>(mip));
            if (desc.row_pitches[mip] != size * 8u ||
                desc.slice_pitches[mip] != static_cast<std::size_t>(size) * size * 8u)
            {
                error = "Environment faces must be tightly packed";
                return false;
            }
            const auto& pixels = desc.mip_pixels[mip];
            for (std::size_t offset = 0u; offset < pixels.size(); offset += 2u)
            {
                const auto bits =
                    static_cast<unsigned>(pixels[offset]) | (static_cast<unsigned>(pixels[offset + 1u]) << 8u);
                if ((bits & 0x8000u) != 0u || (bits & 0x7c00u) == 0x7c00u ||
                    ((offset / 2u) % 4u == 3u && bits != 0x3c00u))
                {
                    error = "Environment radiance must be finite nonnegative FP16 RGB with alpha=1";
                    return false;
                }
            }
        }
        return true;
    }
} // namespace toy3d
