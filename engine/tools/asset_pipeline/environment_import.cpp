#include "asset_pipeline/environment_import.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "image/float16.h"
#include "math/math_constants.h"

namespace toy3d
{
    namespace
    {
        constexpr std::size_t source_limit = 32u * 1024u * 1024u;
        constexpr std::size_t decoded_limit = 128u * 1024u * 1024u;
        constexpr std::uint64_t maximum_sample_work = 128u * 1024u * 1024u;
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, "environment_mips", {}, message, {}};
        }
        Vector3 normalized(const Vector3& value, const Vector3& fallback)
        {
            Vector3 result;
            return try_normalize(value, result) ? result : fallback;
        }
        Vector3 sample_panorama(const RgbaFloatImage& panorama, const Vector3& direction)
        {
            const float u = std::atan2(direction.x, direction.z) / k_two_pi + 0.5f;
            const float v = std::acos(std::max(-1.0f, std::min(1.0f, direction.y))) / k_pi;
            const float x = u * panorama.width - 0.5f, y = v * panorama.height - 0.5f;
            const auto ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
            const float fx = x - ix, fy = y - iy;
            const auto pixel = [&](int px, int py)
            {
                px = (px % static_cast<int>(panorama.width) + static_cast<int>(panorama.width)) %
                     static_cast<int>(panorama.width);
                py = std::max(0, std::min(static_cast<int>(panorama.height) - 1, py));
                const auto offset = (static_cast<std::size_t>(py) * panorama.width + static_cast<std::size_t>(px)) * 4u;
                return Vector3(panorama.pixels[offset], panorama.pixels[offset + 1u], panorama.pixels[offset + 2u]);
            };
            return (pixel(ix, iy) * (1 - fx) + pixel(ix + 1, iy) * fx) * (1 - fy) +
                   (pixel(ix, iy + 1) * (1 - fx) + pixel(ix + 1, iy + 1) * fx) * fy;
        }
        float radical_inverse(std::uint32_t bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
            bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
            bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
            return static_cast<float>(bits) * 2.3283064365386963e-10f;
        }
        Vector3 prefilter(const RgbaFloatImage& panorama, const Vector3& n, float roughness, std::uint32_t samples)
        {
            const float alpha = roughness * roughness;
            const Vector3 axis = std::abs(n.z) < 0.999f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
            const Vector3 tangent = normalized(cross(axis, n), Vector3(1, 0, 0));
            const Vector3 bitangent = cross(n, tangent);
            // Double accumulation avoids overflow near maximum finite FP16 radiance.
            double sum[3] = {0, 0, 0}, weight = 0;
            for (std::uint32_t sample = 0u; sample < samples; ++sample)
            {
                const float phi = k_two_pi * (static_cast<float>(sample) / samples);
                const float xi = radical_inverse(sample);
                const float cos_theta = std::sqrt((1 - xi) / (1 + (alpha * alpha - 1) * xi));
                const float sin_theta = std::sqrt(std::max(0.0f, 1 - cos_theta * cos_theta));
                const Vector3 h =
                    tangent * (std::cos(phi) * sin_theta) + bitangent * (std::sin(phi) * sin_theta) + n * cos_theta;
                const Vector3 l = normalized(h * (2 * dot(n, h)) - n, n);
                const float NoL = std::max(0.0f, dot(n, l));
                if (NoL > 0)
                {
                    const Vector3 radiance = sample_panorama(panorama, l);
                    sum[0] += static_cast<double>(radiance.x) * NoL;
                    sum[1] += static_cast<double>(radiance.y) * NoL;
                    sum[2] += static_cast<double>(radiance.z) * NoL;
                    weight += NoL;
                }
            }
            return weight > 0 ? Vector3(static_cast<float>(sum[0] / weight), static_cast<float>(sum[1] / weight),
                                        static_cast<float>(sum[2] / weight))
                              : sample_panorama(panorama, n);
        }
    } // namespace

    AssetResult<EnvironmentAsset> build_environment_asset(const RgbaFloatImage& panorama,
                                                          EnvironmentImportSettings settings)
    {
        if (panorama.width < 4u || panorama.height < 2u || panorama.height > 2048u || panorama.width > 4096u ||
            panorama.width != panorama.height * 2u ||
            static_cast<std::uint64_t>(panorama.width) * panorama.height * 16u > decoded_limit ||
            panorama.pixels.size() != static_cast<std::size_t>(panorama.width) * panorama.height * 4u ||
            settings.face_size < 2u || settings.face_size > maximum_environment_face_size ||
            (settings.face_size & (settings.face_size - 1u)) != 0u || settings.sample_count < 16u ||
            settings.sample_count > 1024u)
        {
            return AssetResult<EnvironmentAsset>(invalid("Environment requires bounded 2:1 linear HDR panorama, 2..512 "
                                                         "power-of-two faces and 16..1024 samples."));
        }
        const auto mip_count = environment_mip_count(settings.face_size);
        std::uint64_t texels = 0u;
        for (std::uint32_t mip = 1u; mip < mip_count; ++mip)
        {
            const auto size = std::max(1u, settings.face_size >> mip);
            texels += static_cast<std::uint64_t>(size) * size * environment_face_count;
        }
        if (texels * settings.sample_count > maximum_sample_work)
        {
            return AssetResult<EnvironmentAsset>(
                invalid("Environment prefilter work exceeds 128M samples; reduce face size or sample count."));
        }
        for (std::size_t i = 0u; i < panorama.pixels.size(); ++i)
        {
            const float value = panorama.pixels[i];
            if (!std::isfinite(value) || value < 0 || value > 65504.0f)
            {
                return AssetResult<EnvironmentAsset>(invalid("Environment source must be finite nonnegative "
                                                             "FP16-representable radiance; no clamping is applied."));
            }
        }
        EnvironmentAsset candidate;
        candidate.face_size = settings.face_size;
        candidate.mips.resize(mip_count);
        for (std::uint32_t mip = 0u; mip < mip_count; ++mip)
        {
            const auto size = std::max(1u, settings.face_size >> mip);
            const float roughness = static_cast<float>(mip) / (mip_count - 1u);
            for (std::uint32_t face = 0u; face < environment_face_count; ++face)
            {
                auto& pixels = candidate.mips[mip].faces[face];
                pixels.resize(static_cast<std::size_t>(size) * size * 4u);
                for (std::uint32_t y = 0u; y < size; ++y)
                {
                    for (std::uint32_t x = 0u; x < size; ++x)
                    {
                        const auto n = environment_face_direction(face, (x + 0.5f) / size, (y + 0.5f) / size);
                        const auto color = mip == 0u ? sample_panorama(panorama, n)
                                                     : prefilter(panorama, n, roughness, settings.sample_count);
                        const auto offset = (static_cast<std::size_t>(y) * size + x) * 4u;
                        for (std::size_t channel = 0u; channel < 3u; ++channel)
                        {
                            const float value = color.data()[channel];
                            if (value < 0 || !try_encode_float16(value, pixels[offset + channel]))
                            {
                                return AssetResult<EnvironmentAsset>(
                                    invalid("Prefiltered Environment radiance is not finite nonnegative FP16."));
                            }
                        }
                        pixels[offset + 3u] = 0x3c00u;
                    }
                }
            }
        }
        const auto valid = validate_environment_asset(candidate);
        return valid.succeeded() ? AssetResult<EnvironmentAsset>(std::move(candidate))
                                 : AssetResult<EnvironmentAsset>(valid);
    }

    AssetResult<EnvironmentAsset> import_environment_hdr(const std::vector<std::uint8_t>& source,
                                                         EnvironmentImportSettings settings)
    {
        if (source.size() > source_limit)
        {
            return AssetResult<EnvironmentAsset>(invalid("Environment HDR source exceeds 32 MiB."));
        }
        RgbaFloatImage panorama;
        const auto decoded = decode_hdr_image(source, panorama, {4096u, source_limit, decoded_limit});
        if (!decoded.succeeded())
        {
            return AssetResult<EnvironmentAsset>(invalid(decoded.message.c_str()));
        }
        return build_environment_asset(panorama, settings);
    }
} // namespace toy3d
