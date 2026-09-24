#pragma once

#include "serialization/value_codec.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace toy3d
{
    struct SchemaField
    {
        bool required = true;
        std::vector<std::uint8_t> bytes;
    };
    using SchemaFields = std::map<std::string, SchemaField>;
    using SchemaMigrationStep = std::function<ValueStatus(SchemaFields&)>;

    // A step upgrades exactly one schema version. The owner registers all steps
    // before loading assets; migration never changes the input byte vector.
    class SchemaMigrationRegistry
    {
      public:
        bool add_step(std::string type_name, std::uint32_t from_version, SchemaMigrationStep step);
        ValueStatus migrate(const std::string& type_name, std::uint32_t source_version,
                            std::uint32_t target_version, const std::vector<std::uint8_t>& input,
                            std::vector<std::uint8_t>& output, ValueLimits limits = {}) const;

      private:
        std::map<std::string, std::map<std::uint32_t, SchemaMigrationStep>> steps_;
    };

    ValueStatus rename_schema_field(SchemaFields& fields, const std::string& old_name,
                                    const std::string& new_name);
    ValueStatus convert_schema_field(SchemaFields& fields, const std::string& name,
                                     const std::function<ValueStatus(const std::vector<std::uint8_t>&,
                                                                     std::vector<std::uint8_t>&)>& converter);
} // namespace toy3d
