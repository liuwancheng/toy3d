#include "serialization/schema_migration.h"

#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        ValueStatus read_fields(const std::vector<std::uint8_t>& input, ValueLimits limits, SchemaFields& fields)
        {
            ValueReader reader(input, limits);
            std::uint32_t count = 0;
            ValueStatus status = reader.read_array_length(count);
            if (!status.succeeded()) return status;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                std::string name;
                status = reader.read_utf8(name);
                if (!status.succeeded()) return status;
                reader.set_property_path(name);
                if (name.empty() || fields.count(name) != 0)
                    return reader.failure(ValueErrorCode::InvalidValue, "empty or duplicate field name");
                std::uint8_t flags = 0;
                status = reader.read_uint8(flags);
                if (!status.succeeded()) return status;
                if (flags > 1)
                    return reader.failure(ValueErrorCode::InvalidValue, "unknown field flags");
                std::vector<std::uint8_t> bytes;
                status = reader.read_blob(bytes);
                if (!status.succeeded()) return status;
                fields.emplace(std::move(name), SchemaField{flags == 1, std::move(bytes)});
            }
            if (!reader.at_end())
                return reader.failure(ValueErrorCode::InvalidValue, "trailing schema bytes");
            return ValueStatus::success();
        }

        ValueStatus write_fields(const SchemaFields& fields, ValueLimits limits,
                                 std::vector<std::uint8_t>& output)
        {
            if (fields.size() > std::numeric_limits<std::uint32_t>::max())
                return {ValueErrorCode::TooLarge, 0, {}, "schema field count exceeds uint32"};
            ValueWriter writer(limits);
            ValueStatus status = writer.write_array_length(static_cast<std::uint32_t>(fields.size()));
            if (!status.succeeded()) return status;
            for (const auto& field : fields)
            {
                writer.set_property_path(field.first);
                status = writer.write_utf8(field.first);
                if (!status.succeeded()) return status;
                status = writer.write_uint8(field.second.required ? 1u : 0u);
                if (!status.succeeded()) return status;
                status = writer.write_blob(field.second.bytes);
                if (!status.succeeded()) return status;
            }
            output = writer.bytes();
            return ValueStatus::success();
        }
    } // namespace

    ValueStatus decode_schema_fields(const std::vector<std::uint8_t>& bytes, SchemaFields& fields, ValueLimits limits)
    {
        SchemaFields candidate;
        const auto status = read_fields(bytes, limits, candidate);
        if (status.succeeded()) fields = std::move(candidate);
        return status;
    }
    ValueStatus encode_schema_fields(const SchemaFields& fields, std::vector<std::uint8_t>& bytes, ValueLimits limits)
    { return write_fields(fields, limits, bytes); }

    bool SchemaMigrationRegistry::add_step(std::string type_name, std::uint32_t from_version,
                                            SchemaMigrationStep step)
    {
        if (type_name.empty() || from_version == 0 || from_version == std::numeric_limits<std::uint32_t>::max() ||
            !step)
            return false;
        return steps_[std::move(type_name)].emplace(from_version, std::move(step)).second;
    }

    ValueStatus SchemaMigrationRegistry::migrate(const std::string& type_name, std::uint32_t source_version,
                                                  std::uint32_t target_version,
                                                  const std::vector<std::uint8_t>& input,
                                                  std::vector<std::uint8_t>& output, ValueLimits limits) const
    {
        if (source_version == 0 || target_version == 0 || source_version > target_version)
            return {ValueErrorCode::InvalidValue, 0, {}, "unsupported schema version"};
        if (source_version == target_version)
        {
            if (input.size() > limits.max_bytes)
                return {ValueErrorCode::TooLarge, 0, {}, "schema bytes exceed limit"};
            output = input;
            return ValueStatus::success();
        }
        SchemaFields fields;
        ValueStatus status = read_fields(input, limits, fields);
        if (!status.succeeded()) return status;
        const auto type_steps = steps_.find(type_name);
        for (std::uint32_t version = source_version; version < target_version; ++version)
        {
            if (type_steps == steps_.end())
                return {ValueErrorCode::InvalidValue, 0, {}, "schema migration is missing"};
            const auto found = type_steps->second.find(version);
            if (found == type_steps->second.end())
                return {ValueErrorCode::InvalidValue, 0, {}, "schema migration step is missing"};
            status = found->second(fields);
            if (!status.succeeded()) return status;
        }
        std::vector<std::uint8_t> candidate;
        status = write_fields(fields, limits, candidate);
        if (status.succeeded()) output = std::move(candidate);
        return status;
    }

    ValueStatus rename_schema_field(SchemaFields& fields, const std::string& old_name,
                                    const std::string& new_name)
    {
        const auto old = fields.find(old_name);
        if (old_name.empty() || new_name.empty() || old == fields.end() || fields.count(new_name) != 0)
            return {ValueErrorCode::InvalidValue, 0, old_name, "field rename source missing or target exists"};
        fields.emplace(new_name, std::move(old->second));
        fields.erase(old);
        return ValueStatus::success();
    }

    ValueStatus convert_schema_field(SchemaFields& fields, const std::string& name,
                                     const std::function<ValueStatus(const std::vector<std::uint8_t>&,
                                                                     std::vector<std::uint8_t>&)>& converter)
    {
        const auto field = fields.find(name);
        if (field == fields.end() || !converter)
            return {ValueErrorCode::InvalidValue, 0, name, "field conversion source or callback missing"};
        std::vector<std::uint8_t> candidate;
        ValueStatus status = converter(field->second.bytes, candidate);
        if (status.succeeded()) field->second.bytes = std::move(candidate);
        return status;
    }
} // namespace toy3d
