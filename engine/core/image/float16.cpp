#include "image/float16.h"

#include <cmath>

#include <glm/gtc/packing.hpp>

namespace toy3d
{
    bool try_encode_float16(float value, std::uint16_t& bits) noexcept
    {
        if (!std::isfinite(value) || std::abs(value) > 65504.0f)
        {
            return false;
        }
        bits = glm::packHalf1x16(value);
        return (bits & 0x7c00u) != 0x7c00u;
    }

    float decode_float16(std::uint16_t bits) noexcept
    {
        return glm::unpackHalf1x16(bits);
    }
} // namespace toy3d
