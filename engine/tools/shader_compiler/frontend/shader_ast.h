#pragma once

#include "frontend/source_location.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    enum class PropertyType
    {
        Float,
        Float2,
        Float3,
        Float4,
        Color,
        Matrix4x4,
        Range,
        Texture2D,
        TextureCube,
        Sampler
    };

    enum class DefaultValueKind
    {
        None,
        Numbers,
        String,
        Identifier
    };

    struct DefaultValue
    {
        DefaultValueKind kind = DefaultValueKind::None;
        std::vector<double> numbers;
        std::string text;
        SourceLocation location;
    };

    struct Property
    {
        std::string name;
        std::string display_name;
        PropertyType type = PropertyType::Float;
        std::optional<double> range_min;
        std::optional<double> range_max;
        DefaultValue default_value;
        SourceLocation location;
    };

    enum class BindingGroup
    {
        Global,
        View,
        Pass,
        Material,
        Object
    };

    struct Resource
    {
        BindingGroup group = BindingGroup::Pass;
        std::string name;
        std::string type;
        DefaultValue default_value;
        SourceLocation location;
    };

    enum class VariantType
    {
        Boolean,
        Enumeration
    };

    struct Variant
    {
        VariantType type = VariantType::Boolean;
        std::string name;
        std::vector<std::string> options;
        std::string default_value;
        SourceLocation location;
    };

    enum class ShaderStage
    {
        Vertex,
        Pixel,
        Compute
    };

    struct EntryPoint
    {
        ShaderStage stage = ShaderStage::Vertex;
        std::string name;
        SourceLocation location;
    };

    struct HlslBlock
    {
        std::string source;
        SourceLocation location;
        std::vector<EntryPoint> entry_points;
    };

    struct PassState
    {
        std::string name;
        std::string value;
        SourceLocation location;
    };

    struct ShaderPass
    {
        std::string name;
        std::vector<std::string> requirements;
        std::vector<PassState> states;
        HlslBlock program;
        SourceLocation location;
    };

    struct ShaderAsset
    {
        std::string name;
        std::uint32_t version = 0;
        std::vector<Property> properties;
        std::vector<Resource> resources;
        std::vector<Variant> variants;
        std::vector<HlslBlock> includes;
        std::vector<ShaderPass> passes;
        SourceLocation location;
    };
}
