#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toy3d::shader
{
    using Sha256Hash = std::array<std::uint8_t, 32>;

    Sha256Hash sha256(const std::vector<std::uint8_t>& bytes);
    Sha256Hash sha256(std::string_view text);
    std::string sha256_to_hex(const Sha256Hash& hash);
    std::optional<Sha256Hash> sha256_from_hex(const std::string& text);
}
