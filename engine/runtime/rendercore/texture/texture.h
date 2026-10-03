#pragma once

#include "image/pixel_format.h"
#include "image/texture_usage.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    class TextureResource;

    struct TextureDesc
    {
        TextureUsage usage = TextureUsage::Color;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        PixelFormat format = PixelFormat::Unknown;
        std::vector<std::size_t> row_pitches;
        std::vector<std::size_t> slice_pitches;
        // Cube levels concatenate six faces; pitches describe one face.
        std::vector<std::vector<std::uint8_t>> mip_pixels;
        bool cube = false;
        bool requires_linear_filter = false;

        bool validate(std::string& error) const;
    };

    // GT/Asset-side immutable cooked Texture identity. The pointed-to render
    // representation has a stable address, but its mutable state is RT-only.
    class Texture
    {
      public:
        static std::shared_ptr<const Texture> create(TextureDesc desc);
        // The caller must hold the final TextureRef. Normal return transfers
        // that ownership to a RenderCommand and clears the caller reference.
        static void release(std::shared_ptr<const Texture>& texture);
        ~Texture();

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&& other) noexcept;
        Texture& operator=(Texture&&) noexcept = delete;

        const TextureDesc& desc() const
        {
            return desc_;
        }

        // This pointer is an opaque cross-side identity. GT callers must not
        // read or mutate TextureResource state through it.
        TextureResource* texture_resource() const noexcept
        {
            return texture_resource_.get();
        }

      private:
        explicit Texture(TextureDesc desc);

        TextureDesc desc_;
        std::unique_ptr<TextureResource> texture_resource_;
    };

    using TextureRef = std::shared_ptr<const Texture>;
} // namespace toy3d
