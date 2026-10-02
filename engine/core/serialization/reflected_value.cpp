#include "serialization/reflected_value.h"

#include <utility>

namespace toy3d
{
    ValueStatus encode_value(ValueWriter& writer, const ReflectedValue& value)
    {
        if (value.type.empty() || value.schema_version == 0)
        {
            return writer.failure(ValueErrorCode::InvalidValue, "Reflected value needs a type and schema version.");
        }
        auto status = writer.write_utf8(value.type);
        if (status.succeeded())
        {
            status = writer.write_uint32(value.schema_version);
        }
        return status.succeeded() ? writer.write_blob(value.bytes) : status;
    }

    ValueStatus decode_value(ValueReader& reader, ReflectedValue& value)
    {
        ReflectedValue candidate;
        auto status = reader.read_utf8(candidate.type);
        if (status.succeeded())
        {
            status = reader.read_uint32(candidate.schema_version);
        }
        if (status.succeeded())
        {
            status = reader.read_blob(candidate.bytes);
        }
        if (!status.succeeded())
        {
            return status;
        }
        if (candidate.type.empty() || candidate.schema_version == 0)
        {
            return reader.failure(ValueErrorCode::InvalidValue, "Invalid reflected value identity.");
        }
        value = std::move(candidate);
        return ValueStatus::success();
    }
} // namespace toy3d
