#include "asset/texture/environment_asset.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "image/float16.h"

namespace toy3d
{
    namespace
    {
        constexpr std::size_t payload_limit = 32u * 1024u * 1024u;
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, "environment_mips", {}, message, {}};
        }
        bool valid_header(std::uint32_t size, std::uint32_t count, std::uint32_t algorithm, std::uint32_t orientation)
        {
            return size >= 2u && size <= maximum_environment_face_size && (size & (size - 1u)) == 0u &&
                   count == environment_mip_count(size) && algorithm == environment_algorithm_version &&
                   orientation == environment_orientation_version;
        }
    } // namespace

    std::uint32_t environment_mip_count(std::uint32_t face_size)
    {
        std::uint32_t count = 0u;
        while (face_size != 0u)
        {
            ++count;
            face_size >>= 1u;
        }
        return count;
    }

    Vector3 environment_face_direction(std::uint32_t face, float u, float v)
    {
        const float s = 2.0f * u - 1.0f, t = 2.0f * v - 1.0f;
        Vector3 direction;
        switch (face)
        {
        case 0u:
            direction = {1, -t, -s};
            break;
        case 1u:
            direction = {-1, -t, s};
            break;
        case 2u:
            direction = {s, 1, t};
            break;
        case 3u:
            direction = {s, -1, -t};
            break;
        case 4u:
            direction = {s, -t, 1};
            break;
        case 5u:
            direction = {-s, -t, -1};
            break;
        default:
            return {};
        }
        Vector3 normalized;
        return try_normalize(direction, normalized) ? normalized : Vector3{};
    }

    AssetStatus validate_environment_asset(const EnvironmentAsset& environment)
    {
        if (!valid_header(environment.face_size, static_cast<std::uint32_t>(environment.mips.size()),
                          environment.algorithm_version, environment.orientation_version))
        {
            return invalid(
                "Environment requires 2..512 power-of-two faces, a full mip chain and known algorithm/orientation.");
        }
        for (std::size_t mip = 0u; mip < environment.mips.size(); ++mip)
        {
            const auto size = std::max(1u, environment.face_size >> static_cast<std::uint32_t>(mip));
            for (const auto& face : environment.mips[mip].faces)
            {
                if (face.size() != static_cast<std::size_t>(size) * size * 4u)
                {
                    return invalid("Environment mip requires all six tightly packed RGBA16F faces.");
                }
                for (std::size_t channel = 0u; channel < face.size(); ++channel)
                {
                    const auto bits = face[channel];
                    if ((bits & 0x8000u) != 0u || (bits & 0x7c00u) == 0x7c00u ||
                        (channel % 4u == 3u && bits != 0x3c00u))
                    {
                        return invalid("Environment radiance must be finite nonnegative FP16 RGB with alpha=1.");
                    }
                }
            }
        }
        return AssetStatus::success();
    }

    AssetResult<std::vector<std::uint8_t>> encode_environment_mips(const EnvironmentAsset& environment)
    {
        const auto valid = validate_environment_asset(environment);
        if (!valid.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(valid);
        }
        ValueLimits limits;
        limits.max_bytes = payload_limit;
        ValueWriter writer(limits);
        if (!writer.write_uint32(1u).succeeded() || !writer.write_uint32(environment.face_size).succeeded() ||
            !writer.write_uint32(environment.algorithm_version).succeeded() ||
            !writer.write_uint32(environment.orientation_version).succeeded() ||
            !writer.write_uint32(static_cast<std::uint32_t>(environment.mips.size())).succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(invalid("Environment header encoding failed."));
        }
        for (const auto& mip : environment.mips)
        {
            for (const auto& face : mip.faces)
            {
                for (const auto bits : face)
                {
                    if (!writer.write_uint16(bits).succeeded())
                    {
                        return AssetResult<std::vector<std::uint8_t>>(
                            invalid("Environment payload exceeds its byte budget."));
                    }
                }
            }
        }
        return AssetResult<std::vector<std::uint8_t>>(writer.bytes());
    }

    AssetResult<EnvironmentAsset> decode_environment_mips(const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.size() < 20u || bytes.size() > payload_limit)
        {
            return AssetResult<EnvironmentAsset>(
                invalid("Environment payload is truncated or exceeds its byte budget."));
        }
        ValueLimits limits;
        limits.max_bytes = payload_limit;
        ValueReader reader(bytes, limits);
        EnvironmentAsset candidate;
        std::uint32_t version = 0u, count = 0u;
        if (!reader.read_uint32(version).succeeded() || version != 1u ||
            !reader.read_uint32(candidate.face_size).succeeded() ||
            !reader.read_uint32(candidate.algorithm_version).succeeded() ||
            !reader.read_uint32(candidate.orientation_version).succeeded() || !reader.read_uint32(count).succeeded() ||
            !valid_header(candidate.face_size, count, candidate.algorithm_version, candidate.orientation_version))
        {
            return AssetResult<EnvironmentAsset>(invalid("Invalid Environment payload header."));
        }
        std::size_t expected = 20u;
        for (std::uint32_t mip = 0u; mip < count; ++mip)
        {
            const auto size = std::max(1u, candidate.face_size >> mip);
            expected += static_cast<std::size_t>(size) * size * environment_face_count * 8u;
        }
        if (bytes.size() != expected)
        {
            return AssetResult<EnvironmentAsset>(
                invalid("Environment payload must contain the exact complete mip chain."));
        }
        candidate.mips.resize(count);
        for (std::uint32_t mip = 0u; mip < count; ++mip)
        {
            const auto size = std::max(1u, candidate.face_size >> mip);
            for (auto& face : candidate.mips[mip].faces)
            {
                face.resize(static_cast<std::size_t>(size) * size * 4u);
                for (auto& bits : face)
                {
                    if (!reader.read_uint16(bits).succeeded())
                    {
                        return AssetResult<EnvironmentAsset>(invalid("Truncated Environment face."));
                    }
                }
            }
        }
        const auto valid = validate_environment_asset(candidate);
        if (!valid.succeeded())
        {
            return AssetResult<EnvironmentAsset>(valid);
        }
        return AssetResult<EnvironmentAsset>(std::move(candidate));
    }

    AssetResult<AssetPairBytes> encode_environment_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                              const EnvironmentAsset& environment)
    {
        const auto payload = encode_environment_mips(environment);
        if (!payload.succeeded())
        {
            return AssetResult<AssetPairBytes>(payload.status());
        }
        EnvironmentAssetData metadata;
        metadata.face_size = environment.face_size;
        metadata.mip_count = static_cast<std::uint32_t>(environment.mips.size());
        metadata.algorithm_version = environment.algorithm_version;
        metadata.orientation_version = environment.orientation_version;
        ValueWriter writer;
        if (!id.valid() || !encode_value(writer, metadata).succeeded())
        {
            return AssetResult<AssetPairBytes>(invalid("Environment requires a valid asset identity and metadata."));
        }
        AssetFileIndex index;
        index.asset_id = id;
        index.root_type = "toy3d.EnvironmentAssetData";
        index.schema_version = 1u;
        AssetFileLimits limits;
        limits.max_file_bytes = payload_limit + 1024u * 1024u;
        return encode_asset_pair(types, std::move(index), writer.bytes(),
                                 {{"environment_mips", 2u, true, payload.value()}}, limits);
    }

    AssetResult<EnvironmentAsset> read_environment_asset(const FileSystem& files, const VirtualPath& path)
    {
        TypeRegistry types;
        if (!register_texture_asset_types(types).succeeded() || !types.freeze().succeeded())
        {
            return AssetResult<EnvironmentAsset>(invalid("Environment type registration failed."));
        }
        AssetFileLimits limits;
        limits.max_file_bytes = payload_limit + 1024u * 1024u;
        const auto pair = read_asset_pair(types, files, path, limits);
        if (!pair.succeeded())
        {
            return AssetResult<EnvironmentAsset>(pair.status());
        }
        const auto& description = pair.value().description;
        if (description.index.root_type != "toy3d.EnvironmentAssetData" || description.index.schema_version != 1u ||
            !description.has_meta || !description.index.dependencies.empty() || !description.index.subresources.empty())
        {
            return AssetResult<EnvironmentAsset>(invalid("Unsupported Environment root/schema/dependencies."));
        }
        ValueReader reader(description.type_data);
        EnvironmentAssetData metadata;
        if (!decode_value(reader, metadata).succeeded() || !reader.at_end() ||
            !valid_header(metadata.face_size, metadata.mip_count, metadata.algorithm_version,
                          metadata.orientation_version))
        {
            return AssetResult<EnvironmentAsset>(invalid("Invalid Environment metadata."));
        }
        const auto segment =
            std::find_if(pair.value().meta.segments.begin(), pair.value().meta.segments.end(),
                         [](const AssetSegmentData& item)
                         {
                             return item.name == "environment_mips" && item.kind == 2u && item.required;
                         });
        if (segment == pair.value().meta.segments.end())
        {
            return AssetResult<EnvironmentAsset>(invalid("Missing required Environment mip payload."));
        }
        for (const auto& item : pair.value().meta.segments)
        {
            if (item.required && &item != &*segment)
            {
                return AssetResult<EnvironmentAsset>(invalid("Unknown required Environment payload."));
            }
        }
        auto decoded = decode_environment_mips(segment->bytes);
        if (!decoded.succeeded())
        {
            return decoded;
        }
        if (decoded.value().face_size != metadata.face_size || decoded.value().mips.size() != metadata.mip_count ||
            decoded.value().algorithm_version != metadata.algorithm_version ||
            decoded.value().orientation_version != metadata.orientation_version)
        {
            return AssetResult<EnvironmentAsset>(invalid("Environment payload does not match its metadata."));
        }
        return decoded;
    }
} // namespace toy3d
