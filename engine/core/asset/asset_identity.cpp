#include "asset_identity.h"

#include <algorithm>
#include <exception>
#include <random>

namespace toy3d
{
    namespace
    {
        int digit(char value)
        {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            return -1;
        }

        bool parse_id(const std::string& value, std::array<std::uint8_t, 16>& output)
        {
            if (value.size() != output.size() * 2) return false;
            std::array<std::uint8_t, 16> candidate{};
            for (std::size_t index = 0; index < candidate.size(); ++index)
            {
                const int high = digit(value[index * 2]);
                const int low = digit(value[index * 2 + 1]);
                if (high < 0 || low < 0) return false;
                candidate[index] = static_cast<std::uint8_t>((high << 4) | low);
            }
            if (std::all_of(candidate.begin(), candidate.end(), [](std::uint8_t byte) { return byte == 0; }))
                return false;
            output = candidate;
            return true;
        }

        std::string format_id(const std::array<std::uint8_t, 16>& bytes)
        {
            constexpr char k_digits[] = "0123456789abcdef";
            std::string result(bytes.size() * 2, '0');
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                result[index * 2] = k_digits[bytes[index] >> 4];
                result[index * 2 + 1] = k_digits[bytes[index] & 0x0fu];
            }
            return result;
        }

        bool is_valid_id(const std::array<std::uint8_t, 16>& bytes)
        {
            return std::any_of(bytes.begin(), bytes.end(), [](std::uint8_t byte) { return byte != 0; });
        }
    } // namespace

    bool AssetId::valid() const { return is_valid_id(bytes); }
    std::string AssetId::hex() const { return format_id(bytes); }
    bool AssetId::parse(const std::string& hex, AssetId& output) { return parse_id(hex, output.bytes); }
    bool AssetId::try_generate(AssetId& output)
    {
        try
        {
            std::random_device entropy;
            std::uniform_int_distribution<unsigned> distribution(0, 255);
            AssetId candidate;
            for (std::uint8_t& byte : candidate.bytes) byte = static_cast<std::uint8_t>(distribution(entropy));
            if (!candidate.valid()) return false;
            output = candidate;
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    bool SubresourceId::valid() const { return is_valid_id(bytes); }
    std::string SubresourceId::hex() const { return format_id(bytes); }
    bool SubresourceId::parse(const std::string& hex, SubresourceId& output)
    {
        return parse_id(hex, output.bytes);
    }
    bool operator==(const AssetId& left, const AssetId& right) { return left.bytes == right.bytes; }
    bool operator<(const AssetId& left, const AssetId& right) { return left.bytes < right.bytes; }
    bool operator==(const SubresourceId& left, const SubresourceId& right)
    {
        return left.bytes == right.bytes;
    }
    bool operator<(const SubresourceId& left, const SubresourceId& right)
    {
        return left.bytes < right.bytes;
    }
} // namespace toy3d
