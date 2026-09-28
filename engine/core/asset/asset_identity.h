#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace toy3d
{
    struct AssetId
    {
        std::array<std::uint8_t, 16> bytes{};

        bool valid() const;
        std::string hex() const;
        static bool parse(const std::string& hex, AssetId& output);
        // Failure leaves output unchanged; random identity is not a content hash.
        static bool try_generate(AssetId& output);
    };

    struct SubresourceId
    {
        std::array<std::uint8_t, 16> bytes{};

        bool valid() const;
        std::string hex() const;
        static bool parse(const std::string& hex, SubresourceId& output);
    };

    bool operator==(const AssetId& left, const AssetId& right);
    bool operator<(const AssetId& left, const AssetId& right);
    bool operator==(const SubresourceId& left, const SubresourceId& right);
    bool operator<(const SubresourceId& left, const SubresourceId& right);

    enum class AssetRefStrength : std::uint8_t
    {
        Strong = 0,
        Weak = 1,
        Deferred = 2
    };

    struct AssetRef
    {
        AssetId asset_id;
        SubresourceId subresource_id;
        std::string expected_type;
        AssetRefStrength strength = AssetRefStrength::Strong;
    };

    class ValueWriter;
    class ValueReader;
    struct ValueStatus;
    ValueStatus encode_value(ValueWriter& writer, const AssetRef& value);
    ValueStatus decode_value(ValueReader& reader, AssetRef& value);
} // namespace toy3d
