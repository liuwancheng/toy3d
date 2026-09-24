#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace toy3d
{
    enum class ReflectionErrorCode
    {
        None,
        InvalidDescription,
        DuplicateName,
        Frozen
    };

    struct ReflectionStatus
    {
        ReflectionErrorCode code = ReflectionErrorCode::None;
        std::string type_name;
        std::string property_name;
        std::string message;

        bool succeeded() const;
        static ReflectionStatus success();
    };

    enum class PropertyUsage : std::uint32_t
    {
        Edit = 1u,
        Visible = 2u,
        Transient = 4u
    };

    enum class ValueKind
    {
        Unknown,
        Bool,
        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float32,
        Float64,
        Utf8,
        Vector2,
        Vector3,
        Vector4,
        Matrix3,
        Matrix4,
        Quaternion,
        Transform,
        AssetRef,
        Enum,
        Struct,
        Array,
        Variant
    };

    struct ValueTypeDesc
    {
        ValueKind kind = ValueKind::Unknown;
        std::string stable_name;
        std::vector<ValueTypeDesc> arguments;
    };

    struct EditorHint
    {
        std::string category;
        std::string unit;
        std::string asset_type;
        double range_min = 0.0;
        double range_max = 0.0;
        bool has_range = false;
    };

    struct PropertyDesc
    {
        std::string name;
        std::string cpp_type;
        std::uint32_t usage = 0;
        EditorHint hint;
        ValueTypeDesc value_type;
    };

    bool property_is_persisted(const PropertyDesc& property);
    bool property_is_editable(const PropertyDesc& property);
    bool property_is_visible(const PropertyDesc& property);

    struct TypeDesc
    {
        std::string name;
        std::uint32_t schema_version = 0;
        std::vector<PropertyDesc> properties;
        struct EnumValue
        {
            std::string name;
            std::int64_t value = 0;
        };
        std::vector<EnumValue> enum_values;
    };

    class TypeRegistry
    {
      public:
        ReflectionStatus add(TypeDesc description);
        ReflectionStatus freeze();
        const TypeDesc* find(const std::string& persistent_name) const;
        bool frozen() const;

      private:
        std::map<std::string, TypeDesc> types_;
        ReflectionStatus registration_error_;
        bool frozen_ = false;
    };
} // namespace toy3d
