#pragma once

#include <cstdint>
#include <string>

namespace toy3d
{
    inline bool is_valid_utf8(const std::string& text)
    {
        std::size_t index = 0;
        while (index < text.size())
        {
            const auto lead = static_cast<unsigned char>(text[index]);
            std::size_t continuation_count = 0;
            std::uint32_t code_point = 0;
            if (lead <= 0x7fu)
            {
                ++index;
                continue;
            }
            if ((lead & 0xe0u) == 0xc0u)
            {
                continuation_count = 1;
                code_point = lead & 0x1fu;
            }
            else if ((lead & 0xf0u) == 0xe0u)
            {
                continuation_count = 2;
                code_point = lead & 0x0fu;
            }
            else if ((lead & 0xf8u) == 0xf0u)
            {
                continuation_count = 3;
                code_point = lead & 0x07u;
            }
            else
            {
                return false;
            }
            if (index + continuation_count >= text.size())
            {
                return false;
            }
            for (std::size_t offset = 1; offset <= continuation_count; ++offset)
            {
                const auto next = static_cast<unsigned char>(text[index + offset]);
                if ((next & 0xc0u) != 0x80u)
                {
                    return false;
                }
                code_point = (code_point << 6u) | (next & 0x3fu);
            }
            const bool overlong = (continuation_count == 1 && code_point < 0x80u) ||
                                  (continuation_count == 2 && code_point < 0x800u) ||
                                  (continuation_count == 3 && code_point < 0x10000u);
            if (overlong || code_point > 0x10ffffu || (code_point >= 0xd800u && code_point <= 0xdfffu))
            {
                return false;
            }
            index += continuation_count + 1;
        }
        return true;
    }
} // namespace toy3d
