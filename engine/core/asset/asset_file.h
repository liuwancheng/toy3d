#pragma once

#include "asset_identity.h"
#include "file_system/file_system.h"
#include "serialization/value_codec.h"
#include "serialization/schema_migration.h"
#include "reflection/type_registry.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toy3d
{
    inline constexpr std::uint32_t asset_file_version = 1;

    enum class AssetErrorCode
    {
        None,
        Io,
        InvalidFormat,
        UnsupportedVersion,
        TooLarge,
        UnknownRequiredSegment,
        TypeMismatch,
        Schema,
        Value,
        ReadOnly,
        DuplicateIdentity,
        MissingReference,
        DependencyCycle,
        InvalidState,
        Conflict
    };

    struct AssetStatus
    {
        AssetErrorCode code = AssetErrorCode::None;
        AssetId asset_id;
        std::string virtual_path;
        std::string segment;
        std::string property_path;
        std::string message;
        FileStatus file_status;

        bool succeeded() const { return code == AssetErrorCode::None; }
        static AssetStatus success() { return {}; }
    };

    template <typename T> class AssetResult
    {
      public:
        explicit AssetResult(T value) : value_(std::move(value)) {}
        explicit AssetResult(AssetStatus status) : status_(std::move(status)) {}
        bool succeeded() const { return status_.succeeded() && value_.has_value(); }
        const AssetStatus& status() const { return status_; }
        const T& value() const { return value_.value(); }

      private:
        // C++17 optional avoids constructing a fake asset index on failure.
        std::optional<T> value_;
        AssetStatus status_;
    };

    struct AssetSegment
    {
        std::string name;
        std::uint8_t kind = 0;
        bool required = true;
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
    };

    struct AssetSubresource
    {
        SubresourceId id;
        std::string type_name;
    };

    struct AssetFileIndex
    {
        AssetId asset_id;
        std::string root_type;
        std::uint32_t schema_version = 0;
        std::vector<AssetRef> dependencies;
        std::vector<AssetSubresource> subresources;
        std::vector<AssetSegment> segments;
    };

    struct AssetSegmentData
    {
        std::string name;
        std::uint8_t kind = 0;
        bool required = true;
        std::vector<std::uint8_t> bytes;
    };

    struct AssetFileLimits
    {
        std::uint64_t max_file_bytes = 1024ull * 1024ull * 1024ull;
        std::uint32_t max_index_bytes = 4u * 1024u * 1024u;
        std::uint32_t max_entries = 100000u;
    };

    AssetResult<std::vector<std::uint8_t>> encode_asset_file(AssetFileIndex index,
        std::vector<AssetSegmentData> data, AssetFileLimits limits = {});
    AssetResult<AssetFileIndex> inspect_asset(const FileSystem& files, const VirtualPath& path,
                                              AssetFileLimits limits = {});
    AssetResult<AssetFileIndex> inspect_asset_bytes(const std::vector<std::uint8_t>& bytes,
                                                    AssetFileLimits limits = {});
    // Builds a complete candidate while preserving the raw bytes of every other segment.
    AssetResult<std::vector<std::uint8_t>> replace_asset_segments(
        const std::vector<std::uint8_t>& original, const std::vector<AssetSegmentData>& replacements,
        AssetFileLimits limits = {});
    AssetResult<std::vector<std::uint8_t>> read_asset_segment(const FileSystem& files,
        const VirtualPath& path, const AssetId& id, const AssetSegment& segment,
        std::size_t max_bytes);

    using AssetFormatStep = std::function<AssetResult<std::vector<std::uint8_t>>(
        const std::vector<std::uint8_t>&)>;

    class AssetFormatMigrationRegistry
    {
      public:
        bool add_step(std::uint32_t from_version, AssetFormatStep step);
        AssetResult<std::vector<std::uint8_t>> migrate(const std::vector<std::uint8_t>& input,
                                                       AssetFileLimits limits = {}) const;

      private:
        std::map<std::uint32_t, AssetFormatStep> steps_;
    };

    template <typename T, typename Validator>
    AssetStatus load_asset(const TypeRegistry& types, const SchemaMigrationRegistry& migrations,
                           const FileSystem& files, const VirtualPath& path, const std::string& expected_type,
                           T& output, Validator validate, AssetFileLimits file_limits = {},
                           ValueLimits value_limits = {})
    {
        const auto inspected = inspect_asset(files, path, file_limits);
        if (!inspected.succeeded()) return inspected.status();
        const AssetFileIndex& index = inspected.value();
        const TypeDesc* type = types.find(expected_type);
        if (type == nullptr || index.root_type != expected_type)
            return {AssetErrorCode::TypeMismatch, index.asset_id, path.utf8(), {}, {},
                    "root type is absent or differs from requested type", {}};
        const AssetSegment* type_data = nullptr;
        for (const AssetSegment& segment : index.segments)
        {
            if (segment.name == "type_data") type_data = &segment;
            if (segment.kind != 1 && segment.kind != 2)
                return {AssetErrorCode::ReadOnly, index.asset_id, path.utf8(), segment.name, {},
                        "unknown optional segment needs a preserving editor", {}};
        }
        if (type_data == nullptr || type_data->length > value_limits.max_bytes ||
            type_data->length > std::numeric_limits<std::size_t>::max())
            return {AssetErrorCode::TooLarge, index.asset_id, path.utf8(), "type_data", {},
                    "typed data exceeds value limit", {}};
        auto segment_bytes = read_asset_segment(files, path, index.asset_id, *type_data,
                                                value_limits.max_bytes);
        if (!segment_bytes.succeeded()) return segment_bytes.status();
        std::vector<std::uint8_t> migrated;
        const ValueStatus migration = migrations.migrate(expected_type, index.schema_version,
            type->schema_version, segment_bytes.value(), migrated, value_limits);
        if (!migration.succeeded())
            return {AssetErrorCode::Schema, index.asset_id, path.utf8(), "type_data",
                    migration.property_path, migration.message, {}};
        ValueReader reader(migrated, value_limits);
        T candidate{};
        const ValueStatus decoded = decode_value(reader, candidate);
        if (!decoded.succeeded() || !reader.at_end())
        {
            const bool optional = decoded.code == ValueErrorCode::UnknownOptionalField;
            return {optional ? AssetErrorCode::ReadOnly : AssetErrorCode::Value, index.asset_id,
                    path.utf8(), "type_data", decoded.property_path,
                    decoded.succeeded() ? "trailing typed data" : decoded.message, {}};
        }
        AssetStatus valid = validate(candidate);
        if (!valid.succeeded())
        {
            valid.asset_id = index.asset_id;
            valid.virtual_path = path.utf8();
            return valid;
        }
        output = std::move(candidate);
        return AssetStatus::success();
    }

    template <typename T, typename Validator>
    AssetStatus load_asset(const TypeRegistry& types, const AssetFormatMigrationRegistry& formats,
                           const SchemaMigrationRegistry& schemas, const FileSystem& files,
                           const VirtualPath& path, const std::string& expected_type, T& output,
                           Validator validate, AssetFileLimits file_limits = {}, ValueLimits value_limits = {})
    {
        const auto inspected = inspect_asset(files, path, file_limits);
        if (inspected.succeeded())
            return load_asset(types, schemas, files, path, expected_type, output,
                              validate, file_limits, value_limits);
        if (inspected.status().code != AssetErrorCode::UnsupportedVersion)
            return inspected.status();
        if (file_limits.max_file_bytes > std::numeric_limits<std::size_t>::max())
            return {AssetErrorCode::TooLarge, {}, path.utf8(), {}, {},
                    "format migration exceeds host address space", {}};
        auto old_bytes = files.read_binary(path, static_cast<std::size_t>(file_limits.max_file_bytes));
        if (!old_bytes.succeeded())
            return {AssetErrorCode::Io, {}, path.utf8(), {}, {},
                    "old asset read failed", old_bytes.status()};
        auto migrated_file = formats.migrate(old_bytes.value(), file_limits);
        if (!migrated_file.succeeded())
        {
            AssetStatus error = migrated_file.status();
            error.virtual_path = path.utf8();
            return error;
        }
        auto current = inspect_asset_bytes(migrated_file.value(), file_limits);
        if (!current.succeeded())
        {
            AssetStatus error = current.status();
            error.virtual_path = path.utf8();
            return error;
        }
        const AssetFileIndex& index = current.value();
        const TypeDesc* type = types.find(expected_type);
        if (type == nullptr || index.root_type != expected_type)
            return {AssetErrorCode::TypeMismatch, index.asset_id, path.utf8(), {}, {},
                    "migrated root type differs from requested type", {}};
        const AssetSegment* typed = nullptr;
        for (const AssetSegment& segment : index.segments)
        {
            if (segment.name == "type_data") typed = &segment;
            if (segment.kind != 1 && segment.kind != 2)
                return {AssetErrorCode::ReadOnly, index.asset_id, path.utf8(), segment.name, {},
                        "unknown optional segment needs a preserving editor", {}};
        }
        if (typed == nullptr || typed->length > value_limits.max_bytes)
            return {AssetErrorCode::TooLarge, index.asset_id, path.utf8(), "type_data", {},
                    "migrated typed data exceeds value limit", {}};
        const std::vector<std::uint8_t> raw(migrated_file.value().begin() +
            static_cast<std::ptrdiff_t>(typed->offset), migrated_file.value().begin() +
            static_cast<std::ptrdiff_t>(typed->offset + typed->length));
        std::vector<std::uint8_t> current_bytes;
        const ValueStatus migrated_schema = schemas.migrate(expected_type, index.schema_version,
            type->schema_version, raw, current_bytes, value_limits);
        if (!migrated_schema.succeeded())
            return {AssetErrorCode::Schema, index.asset_id, path.utf8(), "type_data",
                    migrated_schema.property_path, migrated_schema.message, {}};
        ValueReader reader(current_bytes, value_limits);
        T candidate{};
        const ValueStatus decoded = decode_value(reader, candidate);
        if (!decoded.succeeded() || !reader.at_end())
            return {decoded.code == ValueErrorCode::UnknownOptionalField ? AssetErrorCode::ReadOnly :
                    AssetErrorCode::Value, index.asset_id, path.utf8(), "type_data",
                    decoded.property_path, decoded.succeeded() ? "trailing typed data" : decoded.message, {}};
        AssetStatus valid = validate(candidate);
        if (!valid.succeeded())
        {
            valid.asset_id = index.asset_id;
            valid.virtual_path = path.utf8();
            return valid;
        }
        output = std::move(candidate);
        return AssetStatus::success();
    }

    template <typename T, typename Validator>
    AssetStatus save_asset(const TypeRegistry& types, const SchemaMigrationRegistry& migrations,
                           FileSystem& files, const VirtualPath& path,
                           AssetFileIndex index, const T& value, Validator validate,
                           std::vector<AssetSegmentData> extra = {}, AssetFileLimits file_limits = {},
                           ValueLimits value_limits = {})
    {
        const TypeDesc* type = types.find(index.root_type);
        if (type == nullptr || type->schema_version == 0)
            return {AssetErrorCode::TypeMismatch, index.asset_id, path.utf8(), {}, {},
                    "root type is not registered", {}};
        const auto existing = inspect_asset(files, path, file_limits);
        if (!existing.succeeded()) return existing.status();
        if (!(existing.value().asset_id == index.asset_id) ||
            existing.value().root_type != index.root_type)
            return {AssetErrorCode::TypeMismatch, index.asset_id, path.utf8(), {}, {},
                    "published asset identity or type changed", {}};
        const AssetSegment* published_type_data = nullptr;
        for (const AssetSegment& segment : existing.value().segments)
        {
            if (segment.name == "type_data") published_type_data = &segment;
            if (segment.kind != 1 && segment.kind != 2)
                return {AssetErrorCode::ReadOnly, index.asset_id, path.utf8(), segment.name, {},
                        "cannot preserve unknown optional segment", {}};
            if (segment.name != "type_data")
            {
                bool supplied = false;
                for (const AssetSegmentData& replacement : extra)
                    if (replacement.name == segment.name && replacement.kind == segment.kind &&
                        replacement.required == segment.required) supplied = true;
                if (!supplied)
                    return {AssetErrorCode::ReadOnly, index.asset_id, path.utf8(), segment.name, {},
                            "existing segment data was not supplied for lossless save", {}};
            }
        }
        if (published_type_data == nullptr)
            return {AssetErrorCode::InvalidFormat, index.asset_id, path.utf8(), "type_data", {},
                    "published type data is missing", {}};
        auto published_bytes = read_asset_segment(files, path, index.asset_id,
                                                  *published_type_data, value_limits.max_bytes);
        if (!published_bytes.succeeded()) return published_bytes.status();
        std::vector<std::uint8_t> current_bytes;
        const ValueStatus migration = migrations.migrate(index.root_type, existing.value().schema_version,
            type->schema_version, published_bytes.value(), current_bytes, value_limits);
        if (!migration.succeeded())
            return {AssetErrorCode::Schema, index.asset_id, path.utf8(), "type_data",
                    migration.property_path, migration.message, {}};
        ValueReader published_reader(current_bytes, value_limits);
        T published_candidate{};
        const ValueStatus published_decode = decode_value(published_reader, published_candidate);
        if (!published_decode.succeeded() || !published_reader.at_end())
            return {published_decode.code == ValueErrorCode::UnknownOptionalField ? AssetErrorCode::ReadOnly :
                    AssetErrorCode::Value, index.asset_id, path.utf8(), "type_data",
                    published_decode.property_path, published_decode.succeeded() ?
                    "published type data has trailing bytes" : published_decode.message, {}};
        AssetStatus valid = validate(value);
        if (!valid.succeeded()) return valid;
        ValueWriter writer(value_limits);
        const ValueStatus encoded = encode_value(writer, value);
        if (!encoded.succeeded())
            return {AssetErrorCode::Value, index.asset_id, path.utf8(), "type_data",
                    encoded.property_path, encoded.message, {}};
        index.schema_version = type->schema_version;
        extra.push_back({"type_data", 1, true, writer.bytes()});
        const AssetId publishing_id = index.asset_id;
        auto file = encode_asset_file(std::move(index), std::move(extra), file_limits);
        if (!file.succeeded())
        {
            AssetStatus status = file.status();
            status.virtual_path = path.utf8();
            return status;
        }
        const FileStatus written = files.write_binary_atomic(path, file.value(), FilePublishMode::Replace);
        if (!written.succeeded())
            return {AssetErrorCode::Io, publishing_id, path.utf8(), {}, {}, "atomic asset publish failed", written};
        return AssetStatus::success();
    }
} // namespace toy3d
