#include "reflection/type_registry.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool validate_value_type(const ValueTypeDesc& value, const std::map<std::string, TypeDesc>& types,
                                 std::string& reason)
        {
            if (value.kind == ValueKind::Unknown)
            {
                reason = "value type is unknown";
                return false;
            }
            if (value.kind == ValueKind::Array)
            {
                if (value.arguments.size() != 1 || !value.stable_name.empty())
                {
                    reason = "array requires one element type";
                    return false;
                }
            }
            else if (value.kind == ValueKind::Variant)
            {
                if (value.arguments.size() < 2 || !value.stable_name.empty())
                {
                    reason = "variant requires at least two branches";
                    return false;
                }
            }
            else if (value.kind == ValueKind::Struct || value.kind == ValueKind::Enum)
            {
                const auto found = types.find(value.stable_name);
                if (found == types.end() || found->second.enum_values.empty() != (value.kind == ValueKind::Struct))
                {
                    reason = "referenced value type is absent or has a different kind";
                    return false;
                }
                if (!value.arguments.empty())
                {
                    reason = "named value type cannot have template arguments";
                    return false;
                }
            }
            else if (!value.arguments.empty() || !value.stable_name.empty())
            {
                reason = "scalar value type cannot have nested arguments";
                return false;
            }
            for (const ValueTypeDesc& argument : value.arguments)
            {
                if (!validate_value_type(argument, types, reason))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    bool ReflectionStatus::succeeded() const
    {
        return code == ReflectionErrorCode::None;
    }

    ReflectionStatus ReflectionStatus::success()
    {
        return {};
    }

    bool property_is_persisted(const PropertyDesc& property)
    {
        return (property.usage & static_cast<std::uint32_t>(PropertyUsage::Transient)) == 0;
    }

    bool property_is_editable(const PropertyDesc& property)
    {
        return (property.usage & static_cast<std::uint32_t>(PropertyUsage::Edit)) != 0;
    }

    bool property_is_visible(const PropertyDesc& property)
    {
        const std::uint32_t visible_mask =
            static_cast<std::uint32_t>(PropertyUsage::Edit) | static_cast<std::uint32_t>(PropertyUsage::Visible);
        return (property.usage & visible_mask) != 0;
    }

    ReflectionStatus TypeRegistry::add(TypeDesc description)
    {
        if (frozen_)
        {
            return {ReflectionErrorCode::Frozen, description.name, {}, "type registry is frozen"};
        }
        if (!registration_error_.succeeded())
        {
            return registration_error_;
        }
        if (description.name.empty() || description.schema_version == 0)
        {
            registration_error_ = {ReflectionErrorCode::InvalidDescription,
                                   description.name,
                                   {},
                                   "type name and schema version must be set"};
            return registration_error_;
        }
        if (types_.find(description.name) != types_.end())
        {
            registration_error_ = {
                ReflectionErrorCode::DuplicateName, description.name, {}, "duplicate persistent type name"};
            return registration_error_;
        }

        if (!description.enum_values.empty() && !description.properties.empty())
        {
            registration_error_ = {ReflectionErrorCode::InvalidDescription,
                                   description.name,
                                   {},
                                   "enum values and object properties cannot coexist"};
            return registration_error_;
        }
        std::set<std::string> enum_names;
        std::set<std::int64_t> enum_numbers;
        for (const TypeDesc::EnumValue& value : description.enum_values)
        {
            if (value.name.empty() || !enum_names.insert(value.name).second || !enum_numbers.insert(value.value).second)
            {
                registration_error_ = {ReflectionErrorCode::InvalidDescription, description.name, value.name,
                                       "enum values require unique names and numbers"};
                return registration_error_;
            }
        }

        std::set<std::string> property_names;
        for (const PropertyDesc& property : description.properties)
        {
            if (property.name.empty() || property.cpp_type.empty())
            {
                registration_error_ = {ReflectionErrorCode::InvalidDescription, description.name, property.name,
                                       "property name and C++ type must be set"};
                return registration_error_;
            }
            if (!property_names.insert(property.name).second)
            {
                registration_error_ = {ReflectionErrorCode::DuplicateName, description.name, property.name,
                                       "duplicate persistent property name"};
                return registration_error_;
            }
            const std::uint32_t edit = static_cast<std::uint32_t>(PropertyUsage::Edit);
            const std::uint32_t visible = static_cast<std::uint32_t>(PropertyUsage::Visible);
            const std::uint32_t transient = static_cast<std::uint32_t>(PropertyUsage::Transient);
            if ((property.usage & ~(edit | visible | transient)) != 0 ||
                ((property.usage & edit) != 0 && (property.usage & (visible | transient)) != 0))
            {
                registration_error_ = {ReflectionErrorCode::InvalidDescription, description.name, property.name,
                                       "invalid property usage combination"};
                return registration_error_;
            }
            if (property.hint.has_range &&
                (!std::isfinite(property.hint.range_min) || !std::isfinite(property.hint.range_max) ||
                 property.hint.range_min > property.hint.range_max))
            {
                registration_error_ = {ReflectionErrorCode::InvalidDescription, description.name, property.name,
                                       "invalid property range hint"};
                return registration_error_;
            }
        }

        std::sort(description.properties.begin(), description.properties.end(),
                  [](const PropertyDesc& left, const PropertyDesc& right)
                  {
                      return left.name < right.name;
                  });
        const std::string name = description.name;
        types_.emplace(name, std::move(description));
        return ReflectionStatus::success();
    }

    ReflectionStatus TypeRegistry::freeze()
    {
        if (!registration_error_.succeeded())
        {
            return registration_error_;
        }
        for (const auto& entry : types_)
        {
            for (const PropertyDesc& property : entry.second.properties)
            {
                std::string reason;
                if (!validate_value_type(property.value_type, types_, reason))
                {
                    registration_error_ = {ReflectionErrorCode::InvalidDescription, entry.first, property.name, reason};
                    return registration_error_;
                }
            }
        }
        frozen_ = true;
        return ReflectionStatus::success();
    }

    const TypeDesc* TypeRegistry::find(const std::string& persistent_name) const
    {
        if (!frozen_)
        {
            return nullptr;
        }
        const auto found = types_.find(persistent_name);
        return found == types_.end() ? nullptr : &found->second;
    }

    bool TypeRegistry::frozen() const
    {
        return frozen_;
    }
} // namespace toy3d
