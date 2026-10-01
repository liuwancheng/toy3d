#include "image/pixel_format.h"

#include <iostream>
#include <limits>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }
} // namespace

int main()
{
    using namespace toy3d;

    check(pixel_format_block_width(PixelFormat::R8G8B8A8UNorm) == 1 &&
              pixel_format_block_height(PixelFormat::R8G8B8A8UNorm) == 1 &&
              pixel_format_bytes_per_block(PixelFormat::R8G8B8A8UNorm) == 4,
          "RGBA8 must use one four-byte texel per block");
    check(pixel_format_block_width(PixelFormat::BC1UNorm) == 4 &&
              pixel_format_block_height(PixelFormat::BC1UNorm) == 4 &&
              pixel_format_bytes_per_block(PixelFormat::BC1UNorm) == 8 &&
              pixel_format_is_block_compressed(PixelFormat::BC1UNorm),
          "BC1 must expose its 4x4 eight-byte block geometry");
    check(pixel_format_block_width(PixelFormat::ASTC6x6) == 6 && pixel_format_block_height(PixelFormat::ASTC6x6) == 6 &&
              pixel_format_bytes_per_block(PixelFormat::ASTC6x6) == 16,
          "ASTC 6x6 must expose its sixteen-byte block geometry");
    std::uint64_t row_pitch = 0;
    std::uint64_t slice_pitch = 0;
    check(pixel_format_calculate_minimum_row_pitch(PixelFormat::BC1UNorm, 5, row_pitch) && row_pitch == 16,
          "BC1 row pitch must round five texels up to two blocks");
    check(pixel_format_calculate_minimum_slice_pitch(PixelFormat::BC1UNorm, 5, 5, slice_pitch) && slice_pitch == 32,
          "BC1 slice pitch must round both dimensions up to complete blocks");
    check(pixel_format_calculate_minimum_slice_pitch(PixelFormat::ASTC6x6, 7, 7, slice_pitch) && slice_pitch == 64,
          "ASTC slice pitch must round both dimensions up independently");
    check(pixel_format_calculate_minimum_row_pitch(PixelFormat::PVRTC2, 1, row_pitch) && row_pitch == 16 &&
              pixel_format_calculate_minimum_slice_pitch(PixelFormat::PVRTC2, 1, 1, slice_pitch) && slice_pitch == 32,
          "PVRTC2 must reserve at least two blocks in each dimension");
    check(!pixel_format_calculate_minimum_row_pitch(PixelFormat::Max, 1, row_pitch) && row_pitch == 0,
          "Max must not produce a usable row pitch");
    check(!pixel_format_calculate_minimum_slice_pitch(PixelFormat::R32G32B32A32Float,
                                                      std::numeric_limits<std::uint32_t>::max(),
                                                      std::numeric_limits<std::uint32_t>::max(), slice_pitch) &&
              slice_pitch == 0,
          "Slice pitch overflow must fail and clear the output");
    check(pixel_format_block_width(PixelFormat::Unknown) == 0 && pixel_format_block_height(PixelFormat::Unknown) == 0 &&
              pixel_format_bytes_per_block(PixelFormat::Unknown) == 0,
          "Unknown must not report usable storage geometry");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "Pixel format tests passed\n";
    return 0;
}
