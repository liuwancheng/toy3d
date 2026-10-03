#pragma once

#include <cstdint>

namespace toy3d
{
    // Finite IEEE binary16 encoding; overflow/NaN/Inf fail rather than clamp.
    bool try_encode_float16(float value, std::uint16_t& bits) noexcept;
    float decode_float16(std::uint16_t bits) noexcept;
} // namespace toy3d
