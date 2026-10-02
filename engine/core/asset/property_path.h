#pragma once

#include "asset_file.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    enum class PropertyPathKind
    {
        Field,
        Index,
        ElementId,
        VariantBranch
    };

    struct PropertyPathPart
    {
        PropertyPathKind kind = PropertyPathKind::Field;
        std::string name;
        std::size_t index = 0;
        std::string identity_property;

        static PropertyPathPart field(std::string name);
        static PropertyPathPart index_at(std::size_t index);
        static PropertyPathPart element_id(std::string identity_property, std::string identity);
        static PropertyPathPart variant_branch(std::string stable_tag);
    };

    using PropertyPath = std::vector<PropertyPathPart>;
    std::string format_property_path(const PropertyPath& path);

    struct PropertyAccess
    {
        ValueTypeDesc value_type;
        PropertyDesc property;
        std::vector<std::uint8_t> value_bytes;
        std::vector<std::uint8_t> root_bytes;
    };

    AssetResult<PropertyAccess> access_property(const TypeRegistry& types, const TypeDesc& root_type,
                                                const std::vector<std::uint8_t>& root_bytes, const PropertyPath& path,
                                                const std::vector<std::uint8_t>* replacement = nullptr,
                                                ValueLimits limits = {});
} // namespace toy3d
