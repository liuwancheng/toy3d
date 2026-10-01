#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    struct Rgba8Image
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        // Top-left origin, tightly packed RGBA. Color conversion belongs to the caller.
        std::vector<std::uint8_t> pixels;
    };

    struct ImageLimits
    {
        std::uint32_t max_dimension = 512;
        std::size_t max_encoded_bytes = 4u * 1024u * 1024u;
    };

    struct ImageStatus
    {
        std::string message;
        bool succeeded() const { return message.empty(); }
    };

    ImageStatus encode_png(const Rgba8Image& image, std::vector<std::uint8_t>& output,
                           ImageLimits limits = {});
    ImageStatus decode_png(const std::vector<std::uint8_t>& bytes, Rgba8Image& output,
                           ImageLimits limits = {});
    // Source import accepts PNG or JPEG with the caller's larger, explicit budget.
    ImageStatus decode_image(const std::vector<std::uint8_t>& bytes, Rgba8Image& output,
                             ImageLimits limits = {});
} // namespace toy3d
