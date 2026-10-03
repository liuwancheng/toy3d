#pragma once

#include "asset/asset_pair.h"
#include "asset/texture/environment_asset_data.h"
#include "math/vector3.h"
#include "texture_asset_reflection.h"

#include <array>
#include <cstdint>
#include <vector>

namespace toy3d
{
    constexpr std::uint32_t environment_face_count = 6u;
    constexpr std::uint32_t maximum_environment_face_size = 512u;
    constexpr std::uint32_t environment_algorithm_version = 1u;
    constexpr std::uint32_t environment_orientation_version = 1u;

    // +X/-X/+Y/-Y/+Z/-Z, each face tightly packed RGBA binary16, v down.
    struct EnvironmentAssetMip
    {
        std::array<std::vector<std::uint16_t>, environment_face_count> faces;
    };
    struct EnvironmentAsset
    {
        std::uint32_t face_size = 0u;
        std::uint32_t algorithm_version = environment_algorithm_version;
        std::uint32_t orientation_version = environment_orientation_version;
        std::vector<EnvironmentAssetMip> mips;
    };

    std::uint32_t environment_mip_count(std::uint32_t face_size);
    Vector3 environment_face_direction(std::uint32_t face, float u, float v);
    AssetStatus validate_environment_asset(const EnvironmentAsset& environment);
    AssetResult<std::vector<std::uint8_t>> encode_environment_mips(const EnvironmentAsset& environment);
    AssetResult<EnvironmentAsset> decode_environment_mips(const std::vector<std::uint8_t>& bytes);
    AssetResult<AssetPairBytes> encode_environment_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                              const EnvironmentAsset& environment);
    AssetResult<EnvironmentAsset> read_environment_asset(const FileSystem& files, const VirtualPath& path);
} // namespace toy3d
