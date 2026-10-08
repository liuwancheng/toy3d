#include "asset_pipeline/environment_import.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "asset/asset_pair_store.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "image/float16.h"

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    toy3d::RgbaFloatImage constant_panorama(float value)
    {
        toy3d::RgbaFloatImage image;
        image.width = 16u;
        image.height = 8u;
        image.pixels.resize(image.width * image.height * 4u, value);
        for (std::size_t i = 3u; i < image.pixels.size(); i += 4u)
        {
            image.pixels[i] = 1.0f;
        }
        return image;
    }

    // Fixed little-endian wire fixtures keep decode coverage independent of the importer.
    void append_uint32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
        {
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void append_half_values(std::vector<std::uint8_t>& bytes, std::size_t count, std::uint16_t value)
    {
        std::array<std::uint8_t, 512> chunk{};
        for (std::size_t i = 0u; i < chunk.size(); i += 2u)
        {
            chunk[i] = static_cast<std::uint8_t>(value & 0xffu);
            chunk[i + 1u] = static_cast<std::uint8_t>(value >> 8u);
        }
        std::size_t remaining = count * 2u;
        while (remaining != 0u)
        {
            const std::size_t take = std::min(remaining, chunk.size());
            bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(take));
            remaining -= take;
        }
    }
} // namespace

int main()
{
    using namespace toy3d;
    std::uint16_t half = 0u;
    check(try_encode_float16(65504.0f, half) && decode_float16(half) == 65504.0f &&
              !try_encode_float16(65505.0f, half) && !try_encode_float16(std::numeric_limits<float>::infinity(), half),
          "Binary16 conversion must reject overflow and non-finite input");
    const auto white = build_environment_asset(constant_panorama(4.0f), {4u, 32u});
    check(white.succeeded() && white.value().mips.size() == 3u, "HDR environment must contain a complete mip chain");
    for (const auto& mip : white.value().mips)
    {
        for (const auto& face : mip.faces)
        {
            for (std::size_t i = 0u; i < face.size(); ++i)
            {
                check(decode_float16(face[i]) == (i % 4u == 3u ? 1.0f : 4.0f),
                      "GGX prefilter must preserve constant HDR radiance");
            }
        }
    }
    const Vector3 centers[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (std::uint32_t face = 0u; face < environment_face_count; ++face)
    {
        check(dot(environment_face_direction(face, 0.5f, 0.5f), centers[face]) > 0.999f,
              "Cube face centers must match +X/-X/+Y/-Y/+Z/-Z");
    }
    auto gradient = constant_panorama(0.0f);
    for (std::uint32_t y = 0u; y < gradient.height; ++y)
    {
        for (std::uint32_t x = 0u; x < gradient.width; ++x)
        {
            const float longitude = ((x + 0.5f) / gradient.width - 0.5f) * 6.28318530718f;
            const float latitude = (y + 0.5f) / gradient.height * 3.14159265359f;
            const auto offset = (y * gradient.width + x) * 4u;
            gradient.pixels[offset] = 1.0f + std::sin(longitude) * std::sin(latitude);
            gradient.pixels[offset + 1u] = 1.0f + std::cos(latitude);
            gradient.pixels[offset + 2u] = 1.0f + std::cos(longitude) * std::sin(latitude);
        }
    }
    const auto directional = build_environment_asset(gradient, {4u, 128u});
    check(directional.succeeded(), "Directional panorama prefilter failed");
    const auto mean_channel = [](const std::vector<std::uint16_t>& face, std::size_t channel)
    {
        double sum = 0;
        for (std::size_t i = channel; i < face.size(); i += 4u)
        {
            sum += decode_float16(face[i]);
        }
        return sum / (face.size() / 4u);
    };
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        const auto positive = channel * 2u;
        const auto negative = positive + 1u;
        const auto& sharp = directional.value().mips.front().faces;
        const auto& rough = directional.value().mips.back().faces;
        check(mean_channel(sharp[positive], channel) > mean_channel(sharp[negative], channel) + 1.0,
              "Panorama conversion swapped a Cube axis or face");
        check(mean_channel(rough[positive], channel) - mean_channel(rough[negative], channel) <
                  mean_channel(sharp[positive], channel) - mean_channel(sharp[negative], channel),
              "Roughness mips must blur directional radiance");
    }
    const auto encoded = encode_environment_mips(white.value());
    check(encoded.succeeded() && decode_environment_mips(encoded.value()).succeeded(),
          "Environment mip roundtrip failed");
    auto damaged = encoded.value();
    damaged[8u] = 2u;
    check(!decode_environment_mips(damaged).succeeded(), "Unknown prefilter algorithm was accepted");
    damaged = encoded.value();
    damaged[12u] = 2u;
    check(!decode_environment_mips(damaged).succeeded(), "Unknown Cube orientation was accepted");
    damaged = encoded.value();
    damaged.pop_back();
    check(!decode_environment_mips(damaged).succeeded(), "Truncated Cube face payload was accepted");
    damaged = encoded.value();
    damaged.push_back(0u);
    check(!decode_environment_mips(damaged).succeeded(), "Trailing Cube data was accepted");
    damaged = encoded.value();
    damaged[20u] = 0u;
    damaged[21u] = 0x7cu;
    check(!decode_environment_mips(damaged).succeeded(), "Infinite Cube radiance was accepted");
    // The largest legal asset decodes one face per bulk read; its 1,048,576 values
    // per face must stay inside the reader's element budget.
    const std::uint32_t maximum_face_values = maximum_environment_face_size * maximum_environment_face_size * 4u;
    std::vector<std::uint8_t> maximum_payload;
    append_uint32(maximum_payload, 1u);
    append_uint32(maximum_payload, maximum_environment_face_size);
    append_uint32(maximum_payload, environment_algorithm_version);
    append_uint32(maximum_payload, environment_orientation_version);
    append_uint32(maximum_payload, environment_mip_count(maximum_environment_face_size));
    for (std::uint32_t mip = 0u; mip < environment_mip_count(maximum_environment_face_size); ++mip)
    {
        const std::uint32_t size = std::max(1u, maximum_environment_face_size >> mip);
        append_half_values(maximum_payload, static_cast<std::size_t>(size) * size * 4u * environment_face_count,
                           0x3c00u);
    }
    const auto maximum = decode_environment_mips(maximum_payload);
    check(maximum.succeeded() && maximum.value().face_size == maximum_environment_face_size &&
              maximum.value().mips.size() == environment_mip_count(maximum_environment_face_size) &&
              maximum.value().mips.front().faces.front().size() == maximum_face_values,
          "Maximum 512-face Environment must decode one face per bulk read");
    auto invalid = constant_panorama(1.0f);
    invalid.pixels[0u] = -1.0f;
    check(!build_environment_asset(invalid, {4u, 32u}).succeeded(),
          "Negative radiance was clamped instead of rejected");
    invalid.pixels[0u] = std::numeric_limits<float>::quiet_NaN();
    check(!build_environment_asset(invalid, {4u, 32u}).succeeded(), "NaN radiance was accepted");
    check(!build_environment_asset(constant_panorama(1.0f), {512u, 1024u}).succeeded(),
          "Unbounded prefilter work was accepted");
    check(build_environment_asset(constant_panorama(65504.0f), {4u, 32u}).succeeded(),
          "Maximum finite FP16 constant radiance must survive prefiltering");
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    std::vector<std::uint8_t> hdr(header.begin(), header.end());
    for (unsigned pixel = 0u; pixel < 8u; ++pixel)
    {
        hdr.insert(hdr.end(), {128u, 128u, 128u, 129u});
    }
    const auto hdr_imported = import_environment_hdr(hdr, {2u, 16u});
    check(hdr_imported.succeeded() && decode_float16(hdr_imported.value().mips[0u].faces[0u][0u]) == 1.0f,
          "Radiance HDR decode did not preserve linear values");
    RgbaFloatImage decoded;
    check(!decode_hdr_image(hdr, decoded, {4096u, 1024u, 1u}).succeeded(), "HDR decoded byte budget was ignored");
    hdr.resize(header.size());
    check(!import_environment_hdr(hdr, {2u, 16u}).succeeded(), "Truncated Radiance input was accepted");

    const std::string rle_header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n";
    std::vector<std::uint8_t> rle(rle_header.begin(), rle_header.end());
    rle.insert(rle.end(), {2u, 2u, 0u, 8u, 136u, 128u, 136u, 128u, 136u, 128u, 136u, 129u});
    check(decode_hdr_image(rle, decoded).succeeded() && decoded.pixels[0u] == 1.0f,
          "Valid RLE Radiance must preserve linear HDR radiance");
    const auto previous_pixels = decoded.pixels;
    auto broken_rle = rle;
    broken_rle.pop_back();
    check(!decode_hdr_image(broken_rle, decoded).succeeded() && decoded.pixels == previous_pixels,
          "Missing RLE repeat value must fail without replacing the previous image");
    broken_rle.resize(rle_header.size() + 4u);
    check(!decode_hdr_image(broken_rle, decoded).succeeded(),
          "Missing RLE channel data must be rejected before entering the stb EOF loop");
    broken_rle = rle;
    broken_rle[rle_header.size() + 4u] = 0u;
    check(!decode_hdr_image(broken_rle, decoded).succeeded(), "Zero-length RLE token was accepted");
    broken_rle = rle;
    broken_rle[rle_header.size() + 4u] = 137u;
    check(!decode_hdr_image(broken_rle, decoded).succeeded(), "RLE run exceeding the scanline was accepted");
    broken_rle = rle;
    broken_rle.push_back(0u);
    check(!decode_hdr_image(broken_rle, decoded).succeeded(), "Trailing RLE payload was accepted");

    TypeRegistry types;
    check(register_texture_asset_types(types).succeeded() && types.freeze().succeeded(),
          "Environment type registration failed");
    AssetId id;
    check(AssetId::try_generate(id), "Environment identity generation failed");
    const auto pair = encode_environment_asset_pair(types, id, white.value());
    check(pair.succeeded() && pair.value().has_meta, "Environment pair encoding failed");
    NativePlatformFile platform;
    const auto root = platform.join_relative(PhysicalPath(TOY3D_ENVIRONMENT_TEST_ROOT), id.hex());
    check(root.succeeded() && platform.create_directories(root.value()).succeeded(),
          "Environment fixture directory failed");
    DirectoryFileStoreDesc descriptor;
    descriptor.physical_root = root.value();
    const auto store = DirectoryFileStore::create(platform, descriptor);
    check(store.succeeded(), "Environment fixture store failed");
    FileSystem files;
    FileMountDesc mount;
    mount.virtual_root = VirtualPath::parse("/Asset").value();
    mount.store = store.value();
    mount.access = MountAccess::ReadWrite;
    check(files.add_mount(mount).succeeded() && files.freeze().succeeded(), "Environment fixture mount failed");
    AssetPairStore assets(types, files);
    const auto path = VirtualPath::parse("/Asset/environment.asset").value();
    check(assets.publish(path, pair.value(), FilePublishMode::CreateNew).succeeded(),
          "Environment pair publish failed");
    const auto loaded = read_environment_asset(files, path);
    check(loaded.succeeded() && loaded.value().mips[2u].faces[5u] == white.value().mips[2u].faces[5u],
          "Current Environment reader failed the published asset");
    check(files.remove_file(VirtualPath::parse("/Asset/environment.meta").value()).succeeded() &&
              !read_environment_asset(files, path).succeeded(),
          "Missing Environment payload was accepted");
    return EXIT_SUCCESS;
}
