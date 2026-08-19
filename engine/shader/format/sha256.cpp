#include "format/sha256.h"

#include <array>
#include <cstddef>

namespace toy3d::shader
{
    // This implementation uses string_view for non-owning text hashing and
    // optional to reject malformed hexadecimal hashes without sentinel data.
    namespace
    {
        constexpr std::array<std::uint32_t, 64> round_constants = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

        std::uint32_t rotate_right(std::uint32_t value, std::uint32_t count)
        {
            return (value >> count) | (value << (32u - count));
        }

        void append_u64_big_endian(std::vector<std::uint8_t>& bytes, std::uint64_t value)
        {
            for (int shift = 56; shift >= 0; shift -= 8)
            {
                bytes.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        }
    }

    Sha256Hash sha256(const std::vector<std::uint8_t>& input)
    {
        std::vector<std::uint8_t> bytes = input;
        const std::uint64_t bit_count = static_cast<std::uint64_t>(bytes.size()) * 8u;
        bytes.push_back(0x80u);
        while ((bytes.size() % 64u) != 56u)
        {
            bytes.push_back(0u);
        }
        append_u64_big_endian(bytes, bit_count);

        std::array<std::uint32_t, 8> state = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        for (std::size_t block = 0; block < bytes.size(); block += 64u)
        {
            std::array<std::uint32_t, 64> words{};
            for (std::size_t index = 0; index < 16u; ++index)
            {
                const std::size_t offset = block + index * 4u;
                words[index] = (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
                    (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u) |
                    (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u) |
                    static_cast<std::uint32_t>(bytes[offset + 3u]);
            }
            for (std::size_t index = 16u; index < words.size(); ++index)
            {
                const std::uint32_t s0 = rotate_right(words[index - 15u], 7u) ^ rotate_right(words[index - 15u], 18u) ^ (words[index - 15u] >> 3u);
                const std::uint32_t s1 = rotate_right(words[index - 2u], 17u) ^ rotate_right(words[index - 2u], 19u) ^ (words[index - 2u] >> 10u);
                words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
            }

            std::uint32_t a = state[0];
            std::uint32_t b = state[1];
            std::uint32_t c = state[2];
            std::uint32_t d = state[3];
            std::uint32_t e = state[4];
            std::uint32_t f = state[5];
            std::uint32_t g = state[6];
            std::uint32_t h = state[7];
            for (std::size_t index = 0; index < words.size(); ++index)
            {
                const std::uint32_t sum1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
                const std::uint32_t choice = (e & f) ^ (~e & g);
                const std::uint32_t temp1 = h + sum1 + choice + round_constants[index] + words[index];
                const std::uint32_t sum0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
                const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                const std::uint32_t temp2 = sum0 + majority;
                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }

        Sha256Hash result{};
        for (std::size_t index = 0; index < state.size(); ++index)
        {
            result[index * 4u] = static_cast<std::uint8_t>(state[index] >> 24u);
            result[index * 4u + 1u] = static_cast<std::uint8_t>(state[index] >> 16u);
            result[index * 4u + 2u] = static_cast<std::uint8_t>(state[index] >> 8u);
            result[index * 4u + 3u] = static_cast<std::uint8_t>(state[index]);
        }
        return result;
    }

    Sha256Hash sha256(std::string_view text)
    {
        return sha256(std::vector<std::uint8_t>(text.begin(), text.end()));
    }

    std::string sha256_to_hex(const Sha256Hash& hash)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(hash.size() * 2u);
        for (const std::uint8_t value : hash)
        {
            result.push_back(digits[value >> 4u]);
            result.push_back(digits[value & 0x0fu]);
        }
        return result;
    }

    std::optional<Sha256Hash> sha256_from_hex(const std::string& text)
    {
        if (text.size() != Sha256Hash{}.size() * 2u) return std::nullopt;
        Sha256Hash result{};
        const auto value = [](char character) -> int {
            if (character >= '0' && character <= '9') return character - '0';
            if (character >= 'a' && character <= 'f') return character - 'a' + 10;
            if (character >= 'A' && character <= 'F') return character - 'A' + 10;
            return -1;
        };
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            const int high = value(text[index * 2u]);
            const int low = value(text[index * 2u + 1u]);
            if (high < 0 || low < 0) return std::nullopt;
            result[index] = static_cast<std::uint8_t>((high << 4) | low);
        }
        return result;
    }
}
