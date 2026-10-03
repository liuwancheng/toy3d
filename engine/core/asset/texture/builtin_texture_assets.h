#pragma once

#include "image/texture_usage.h"

#include <array>

namespace toy3d
{
    struct BuiltinTextureAsset
    {
        const char* default_name;
        const char* asset_name;
        const char* asset_id;
        TextureUsage usage;
    };

    // C++17 inline data gives offline producers and asset consumers one stable
    // identity table without mutable process-wide state or runtime ownership.
    inline constexpr std::array<BuiltinTextureAsset, 5u> builtin_texture_assets = {{
        {"white", "T_White", "26e14823067241ee84676de813b2e8c3", TextureUsage::Color},
        {"black", "T_Black", "a2d7e64e85c940e583f9ac71f5c44833", TextureUsage::Color},
        {"brick", "T_RedBrick", "a6a53651e980491294a675d094006da0", TextureUsage::Color},
        {"normal_flat", "T_NormalFlat", "7cee744dd37b48f9bb45c3dce812bd4e", TextureUsage::Normal},
        {"white_linear", "T_WhiteLinear", "5b60e13d1b4a4398a2103b1d9512390d", TextureUsage::LinearData},
    }};
    inline constexpr const char* builtin_studio_environment_id = "407ebd6e5a4f4d17b8f36e1f088c3afd";
    inline constexpr const char* builtin_courtyard_environment_id = "813b99d8e0ce444dad0c9fc38ce2b01b";
} // namespace toy3d
