#pragma once

#include "format/shader_format_types.h"
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
        Sampler,
        ComparisonSampler
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
        // Each bound is independently optional because a property may specify
        // no range, a one-sided range, or both bounds.
        std::optional<double> range_min;
        std::optional<double> range_max;
        DefaultValue default_value;
        SourceLocation location;
    };

    enum class ResourceElementType
    {
        None,
        Float,
        Float2,
        Float3,
        Float4,
        Int,
        Int2,
        Int3,
        Int4,
        UInt,
        UInt2,
        UInt3,
        UInt4,
        Float2x2,
        Float2x3,
        Float2x4,
        Float3x2,
        Float3x3,
        Float3x4,
        Float4x2,
        Float4x3,
        Float4x4
    };

    struct Resource
    {
        BindingGroup group = BindingGroup::Pass;
        std::string name;
        ResourceKind kind = ResourceKind::Texture2D;
        ResourceElementType element_type = ResourceElementType::None;
        DefaultValue default_value;
        SourceLocation location;
    };

    struct Parameter
    {
        BindingGroup group = BindingGroup::Pass;
        std::string name;
        ShaderValueType type = ShaderValueType::Float32;
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

    struct ShaderPass
    {
        std::string name;
        std::vector<std::string> requirements;
        ShaderGraphicsPassState state;
        bool has_explicit_graphics_state = false;
        HlslBlock program;
        SourceLocation location;
    };

    struct ShaderAsset
    {
        std::string name;
        std::uint32_t version = 0;
        std::vector<Property> properties;
        std::vector<Parameter> parameters;
        std::vector<Resource> resources;
        std::vector<Variant> variants;
        std::vector<HlslBlock> includes;
        std::vector<ShaderPass> passes;
        SourceLocation location;
    };
}
