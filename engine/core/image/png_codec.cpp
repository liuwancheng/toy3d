#include "png_codec.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <sstream>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace toy3d
{
    namespace
    {
        bool valid_size(std::uint32_t width, std::uint32_t height, ImageLimits limits)
        {
            return width != 0 && height != 0 && width <= limits.max_dimension && height <= limits.max_dimension &&
                   width <= static_cast<unsigned>(std::numeric_limits<int>::max() / 4) &&
                   static_cast<std::uint64_t>(width) * height * 4 <= std::numeric_limits<std::size_t>::max() &&
                   static_cast<std::uint64_t>(width) * height * 4 <= limits.max_decoded_bytes;
        }

        bool validate_hdr_stream(const std::vector<std::uint8_t>& bytes, ImageLimits limits, int& width, int& height)
        {
            // Validate before stb: its HDR RLE reader treats EOF as a zero count
            // without advancing, and also accepts truncated raw/repeat payloads.
            std::size_t offset = 0u;
            const auto read_line = [&](std::string& line)
            {
                line.clear();
                while (offset < bytes.size() && offset < 16u * 1024u)
                {
                    const auto byte = bytes[offset++];
                    if (byte == '\n')
                    {
                        return true;
                    }
                    if (byte == 0u || byte == '\r' || line.size() >= 1022u)
                    {
                        return false;
                    }
                    line.push_back(static_cast<char>(byte));
                }
                return false;
            };
            std::string line;
            if (!read_line(line) || (line != "#?RADIANCE" && line != "#?RGBE"))
            {
                return false;
            }
            bool has_format = false;
            do
            {
                if (!read_line(line))
                {
                    return false;
                }
                has_format = has_format || line == "FORMAT=32-bit_rle_rgbe";
            } while (!line.empty());
            if (!has_format || !read_line(line) || line.compare(0u, 3u, "-Y ") != 0)
            {
                return false;
            }
            std::istringstream resolution(line);
            std::string y_axis, x_axis, trailing;
            std::uint64_t w = 0u, h = 0u;
            if (!(resolution >> y_axis >> h >> x_axis >> w) || y_axis != "-Y" || x_axis != "+X" ||
                (resolution >> trailing) || w > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
                h > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
                !valid_size(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), limits) ||
                w * h * 4u * sizeof(float) > limits.max_decoded_bytes)
            {
                return false;
            }
            width = static_cast<int>(w);
            height = static_cast<int>(h);
            const bool rle = w >= 8u && w < 32768u && bytes.size() - offset >= 4u && bytes[offset] == 2u &&
                             bytes[offset + 1u] == 2u && (bytes[offset + 2u] & 0x80u) == 0u;
            if (!rle)
            {
                return bytes.size() - offset == w * h * 4u;
            }
            for (std::uint64_t row = 0u; row < h; ++row)
            {
                if (bytes.size() - offset < 4u || bytes[offset] != 2u || bytes[offset + 1u] != 2u ||
                    ((static_cast<unsigned>(bytes[offset + 2u]) << 8u) | bytes[offset + 3u]) != w)
                {
                    return false;
                }
                offset += 4u;
                for (unsigned channel = 0u; channel < 4u; ++channel)
                {
                    std::uint64_t written = 0u;
                    while (written < w)
                    {
                        if (offset == bytes.size())
                        {
                            return false;
                        }
                        const unsigned count = bytes[offset++];
                        const unsigned pixels = count > 128u ? count - 128u : count;
                        const unsigned encoded_bytes = count > 128u ? 1u : count;
                        if (pixels == 0u || pixels > w - written || encoded_bytes > bytes.size() - offset)
                        {
                            return false;
                        }
                        written += pixels;
                        offset += encoded_bytes;
                    }
                }
            }
            return offset == bytes.size();
        }

        std::uint32_t png_uint32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
        {
            return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
                   (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
                   (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
        }

        bool valid_png_chunks(const std::vector<std::uint8_t>& bytes)
        {
            constexpr std::size_t signature_bytes = 8;
            constexpr std::size_t chunk_overhead = 12;
            constexpr std::uint32_t ihdr = 0x49484452;
            constexpr std::uint32_t idat = 0x49444154;
            constexpr std::uint32_t iend = 0x49454e44;
            bool found_header = false;
            bool found_data = false;
            std::size_t offset = signature_bytes;
            while (bytes.size() - offset >= chunk_overhead)
            {
                const auto length = png_uint32(bytes, offset);
                const auto type = png_uint32(bytes, offset + 4);
                if (length > bytes.size() - offset - chunk_overhead)
                {
                    return false;
                }
                if ((!found_header && (type != ihdr || length != 13)) || (found_header && type == ihdr))
                {
                    return false;
                }
                // Validate PNG's chunk CRC here because stb intentionally omits
                // CRC checks; corrupted cache bytes must trigger regeneration.
                std::uint32_t crc = 0xffffffffu;
                for (std::size_t i = offset + 4; i < offset + 8 + length; ++i)
                {
                    crc ^= bytes[i];
                    constexpr unsigned bits_per_byte = 8;
                    for (unsigned bit = 0; bit < bits_per_byte; ++bit)
                    {
                        crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
                    }
                }
                if ((crc ^ 0xffffffffu) != png_uint32(bytes, offset + 8 + length))
                {
                    return false;
                }
                found_header = true;
                found_data = found_data || type == idat;
                offset += chunk_overhead + length;
                if (type == iend)
                {
                    return length == 0 && found_data && offset == bytes.size();
                }
            }
            return false;
        }

        struct PngOutput
        {
            std::vector<std::uint8_t> bytes;
            std::size_t limit = 0;
            bool exceeded = false;
        };

        void append_png(void* context, void* bytes, int count)
        {
            auto& output = *static_cast<PngOutput*>(context);
            if (count < 0 || static_cast<std::size_t>(count) > output.limit - output.bytes.size())
            {
                output.exceeded = true;
                return;
            }
            const auto* begin = static_cast<const std::uint8_t*>(bytes);
            output.bytes.insert(output.bytes.end(), begin, begin + count);
        }
    } // namespace

    ImageStatus encode_png(const Rgba8Image& image, std::vector<std::uint8_t>& output, ImageLimits limits)
    {
        if (!valid_size(image.width, image.height, limits) ||
            image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4)
        {
            return {"PNG input dimensions or pixel count are invalid."};
        }
        PngOutput candidate{{}, limits.max_encoded_bytes, false};
        const int written = stbi_write_png_to_func(append_png, &candidate, static_cast<int>(image.width),
                                                   static_cast<int>(image.height), 4, image.pixels.data(),
                                                   static_cast<int>(image.width * 4));
        if (written == 0 || candidate.exceeded || candidate.bytes.empty())
        {
            return {"PNG encoding failed or exceeded the encoded byte limit."};
        }
        output = std::move(candidate.bytes);
        return {};
    }

    ImageStatus decode_png(const std::vector<std::uint8_t>& bytes, Rgba8Image& output, ImageLimits limits)
    {
        constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
        if (bytes.size() < signature.size() || bytes.size() > limits.max_encoded_bytes ||
            bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            !std::equal(signature.begin(), signature.end(), bytes.begin()))
        {
            return {"PNG signature or encoded byte count is invalid."};
        }
        if (!valid_png_chunks(bytes))
        {
            return {"PNG chunks are truncated, corrupted, or contain trailing bytes."};
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) ||
            width <= 0 || height <= 0 ||
            !valid_size(static_cast<unsigned>(width), static_cast<unsigned>(height), limits))
        {
            return {"PNG dimensions exceed the decode limit or the header is invalid."};
        }
        // The third-party allocation is released by its matching deleter; no mapped storage escapes.
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4),
            &stbi_image_free);
        if (!pixels)
        {
            return {"PNG pixel decoding failed."};
        }
        Rgba8Image candidate;
        candidate.width = static_cast<unsigned>(width);
        candidate.height = static_cast<unsigned>(height);
        candidate.pixels.assign(pixels.get(), pixels.get() + static_cast<std::size_t>(width) * height * 4);
        output = std::move(candidate);
        return {};
    }

    ImageStatus decode_hdr_image(const std::vector<std::uint8_t>& bytes, RgbaFloatImage& output, ImageLimits limits)
    {
        if (bytes.empty() || bytes.size() > limits.max_encoded_bytes ||
            bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            !stbi_is_hdr_from_memory(bytes.data(), static_cast<int>(bytes.size())))
        {
            return {"HDR input must be a bounded Radiance stream."};
        }
        int width = 0, height = 0, channels = 0;
        if (!validate_hdr_stream(bytes, limits, width, height))
        {
            return {"HDR requires bounded -Y/+X Radiance with exact complete raw/RLE scanlines."};
        }
        std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
            stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4),
            &stbi_image_free);
        if (!pixels)
        {
            return {"Radiance HDR decoding failed."};
        }
        RgbaFloatImage candidate;
        candidate.width = static_cast<unsigned>(width);
        candidate.height = static_cast<unsigned>(height);
        candidate.pixels.assign(pixels.get(), pixels.get() + static_cast<std::size_t>(width) * height * 4u);
        output = std::move(candidate);
        return {};
    }

    ImageStatus decode_image(const std::vector<std::uint8_t>& bytes, Rgba8Image& output, ImageLimits limits)
    {
        constexpr std::array<std::uint8_t, 8> png_signature{137, 80, 78, 71, 13, 10, 26, 10};
        if (bytes.size() >= png_signature.size() &&
            std::equal(png_signature.begin(), png_signature.end(), bytes.begin()))
        {
            return decode_png(bytes, output, limits);
        }
        if (bytes.size() < 4 || bytes.size() > limits.max_encoded_bytes ||
            bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || bytes[0] != 0xff ||
            bytes[1] != 0xd8 || bytes[bytes.size() - 2] != 0xff || bytes[bytes.size() - 1] != 0xd9)
        {
            return {"Image input must be a bounded PNG or complete JPEG stream."};
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) ||
            width <= 0 || height <= 0 ||
            !valid_size(static_cast<unsigned>(width), static_cast<unsigned>(height), limits))
        {
            return {"JPEG dimensions exceed the decode limit or the header is invalid."};
        }
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4),
            &stbi_image_free);
        if (!pixels)
        {
            return {"JPEG pixel decoding failed."};
        }
        Rgba8Image candidate;
        candidate.width = static_cast<unsigned>(width);
        candidate.height = static_cast<unsigned>(height);
        candidate.pixels.assign(pixels.get(), pixels.get() + static_cast<std::size_t>(width) * height * 4);
        output = std::move(candidate);
        return {};
    }
} // namespace toy3d
