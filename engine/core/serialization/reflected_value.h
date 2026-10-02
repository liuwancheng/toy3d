#pragma once

#include "serialization/value_codec.h"

namespace toy3d
{
    // A self-owned, versioned reflected struct, independent of runtime objects.
    struct ReflectedValue
    {
        std::string type;
        std::uint32_t schema_version = 0;
        std::vector<std::uint8_t> bytes;
    };

    ValueStatus encode_value(ValueWriter& writer, const ReflectedValue& value);
    ValueStatus decode_value(ValueReader& reader, ReflectedValue& value);
} // namespace toy3d
