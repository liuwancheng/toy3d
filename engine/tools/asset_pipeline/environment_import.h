#pragma once

#include "asset/texture/environment_asset.h"
#include "image/png_codec.h"

namespace toy3d
{
    struct EnvironmentImportSettings
    {
        std::uint32_t face_size = 64u;
        std::uint32_t sample_count = 128u;
    };
    // Pure CPU prefilter: mip roughness=mip/(mips-1), N=V importance sampling, NoL normalization.
    AssetResult<EnvironmentAsset> build_environment_asset(const RgbaFloatImage& panorama,
                                                          EnvironmentImportSettings settings = {});
    AssetResult<EnvironmentAsset> import_environment_hdr(const std::vector<std::uint8_t>& source,
                                                         EnvironmentImportSettings settings = {});
} // namespace toy3d
