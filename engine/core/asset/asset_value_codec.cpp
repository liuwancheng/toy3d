#include "asset_identity.h"

#include "serialization/value_codec.h"

#include <utility>

namespace toy3d
{
    ValueStatus encode_value(ValueWriter& writer, const AssetRef& value)
    {
        if (!value.asset_id.valid() || value.expected_type.empty())
        {
            return writer.failure(ValueErrorCode::InvalidValue, "invalid asset reference identity or type");
        }
        if (static_cast<std::uint8_t>(value.strength) > 2)
        {
            return writer.failure(ValueErrorCode::InvalidValue, "invalid asset reference strength");
        }
        for (std::uint8_t byte : value.asset_id.bytes)
        {
            const ValueStatus status = writer.write_uint8(byte);
            if (!status.succeeded())
            {
                return status;
            }
        }
        for (std::uint8_t byte : value.subresource_id.bytes)
        {
            const ValueStatus status = writer.write_uint8(byte);
            if (!status.succeeded())
            {
                return status;
            }
        }
        ValueStatus status = writer.write_utf8(value.expected_type);
        if (!status.succeeded())
        {
            return status;
        }
        return writer.write_uint8(static_cast<std::uint8_t>(value.strength));
    }

    ValueStatus decode_value(ValueReader& reader, AssetRef& value)
    {
        AssetRef candidate;
        for (std::uint8_t& byte : candidate.asset_id.bytes)
        {
            const ValueStatus status = reader.read_uint8(byte);
            if (!status.succeeded())
            {
                return status;
            }
        }
        for (std::uint8_t& byte : candidate.subresource_id.bytes)
        {
            const ValueStatus status = reader.read_uint8(byte);
            if (!status.succeeded())
            {
                return status;
            }
        }
        ValueStatus status = reader.read_utf8(candidate.expected_type);
        if (!status.succeeded())
        {
            return status;
        }
        std::uint8_t strength = 0;
        status = reader.read_uint8(strength);
        if (!status.succeeded())
        {
            return status;
        }
        if (!candidate.asset_id.valid() || candidate.expected_type.empty() || strength > 2)
        {
            return reader.failure(ValueErrorCode::InvalidValue, "invalid asset reference identity, type or strength");
        }
        candidate.strength = static_cast<AssetRefStrength>(strength);
        value = std::move(candidate);
        return ValueStatus::success();
    }
} // namespace toy3d
