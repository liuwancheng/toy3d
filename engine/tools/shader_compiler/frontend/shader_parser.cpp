#include "frontend/shader_parser.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        template<typename Collection>
        bool contains_name(const Collection& values, const std::string& name)
        {
            return std::any_of(values.begin(), values.end(), [&](const auto& value) {
                return value.name == name;
            });
        }

        bool is_one_of(std::string_view value, std::initializer_list<std::string_view> choices)
        {
            return std::find(choices.begin(), choices.end(), value) != choices.end();
        }

        std::optional<PropertyType> property_type_from_name(std::string_view name)
        {
            if (name == "Float") return PropertyType::Float;
            if (name == "Float2") return PropertyType::Float2;
            if (name == "Float3") return PropertyType::Float3;
            if (name == "Float4") return PropertyType::Float4;
            if (name == "Color") return PropertyType::Color;
            if (name == "Matrix4x4") return PropertyType::Matrix4x4;
            if (name == "Range") return PropertyType::Range;
            if (name == "Texture2D") return PropertyType::Texture2D;
            if (name == "TextureCube") return PropertyType::TextureCube;
            if (name == "Sampler") return PropertyType::Sampler;
            if (name == "ComparisonSampler") return PropertyType::ComparisonSampler;
            return std::nullopt;
        }

        std::optional<ResourceKind> resource_kind_from_name(std::string_view name)
        {
            if (name == "Texture2D") return ResourceKind::Texture2D;
            if (name == "Texture2DArray") return ResourceKind::Texture2DArray;
            if (name == "Texture3D") return ResourceKind::Texture3D;
            if (name == "TextureCube") return ResourceKind::TextureCube;
            if (name == "Texture2DMS") return ResourceKind::Texture2DMS;
            if (name == "Sampler") return ResourceKind::Sampler;
            if (name == "ComparisonSampler") return ResourceKind::ComparisonSampler;
            if (name == "Buffer") return ResourceKind::Buffer;
            if (name == "ByteAddressBuffer") return ResourceKind::ByteAddressBuffer;
            if (name == "StructuredBuffer") return ResourceKind::StructuredBuffer;
            if (name == "RWBuffer") return ResourceKind::RWBuffer;
            if (name == "RWByteAddressBuffer") return ResourceKind::RWByteAddressBuffer;
            if (name == "RWStructuredBuffer") return ResourceKind::RWStructuredBuffer;
            if (name == "RWTexture2D") return ResourceKind::RWTexture2D;
            if (name == "RWTexture2DArray") return ResourceKind::RWTexture2DArray;
            if (name == "RWTexture3D") return ResourceKind::RWTexture3D;
            return std::nullopt;
        }

        std::optional<ResourceElementType> resource_element_type_from_name(std::string_view name)
        {
            if (name == "Float") return ResourceElementType::Float;
            if (name == "Float2") return ResourceElementType::Float2;
            if (name == "Float3") return ResourceElementType::Float3;
            if (name == "Float4") return ResourceElementType::Float4;
            if (name == "Int") return ResourceElementType::Int;
            if (name == "Int2") return ResourceElementType::Int2;
            if (name == "Int3") return ResourceElementType::Int3;
            if (name == "Int4") return ResourceElementType::Int4;
            if (name == "UInt") return ResourceElementType::UInt;
            if (name == "UInt2") return ResourceElementType::UInt2;
            if (name == "UInt3") return ResourceElementType::UInt3;
            if (name == "UInt4") return ResourceElementType::UInt4;
            if (name == "Float2x2") return ResourceElementType::Float2x2;
            if (name == "Float2x3") return ResourceElementType::Float2x3;
            if (name == "Float2x4") return ResourceElementType::Float2x4;
            if (name == "Float3x2") return ResourceElementType::Float3x2;
            if (name == "Float3x3") return ResourceElementType::Float3x3;
            if (name == "Float3x4") return ResourceElementType::Float3x4;
            if (name == "Float4x2") return ResourceElementType::Float4x2;
            if (name == "Float4x3") return ResourceElementType::Float4x3;
            if (name == "Float4x4") return ResourceElementType::Float4x4;
            return std::nullopt;
        }

        bool is_matrix(ResourceElementType type)
        {
            switch (type)
            {
            case ResourceElementType::Float2x2:
            case ResourceElementType::Float2x3:
            case ResourceElementType::Float2x4:
            case ResourceElementType::Float3x2:
            case ResourceElementType::Float3x3:
            case ResourceElementType::Float3x4:
            case ResourceElementType::Float4x2:
            case ResourceElementType::Float4x3:
            case ResourceElementType::Float4x4:
                return true;
            default:
                return false;
            }
        }

        bool requires_element_type(ResourceKind kind)
        {
            return kind != ResourceKind::Sampler && kind != ResourceKind::ComparisonSampler &&
                kind != ResourceKind::ByteAddressBuffer && kind != ResourceKind::RWByteAddressBuffer;
        }

        bool allows_matrix_element(ResourceKind kind)
        {
            return kind == ResourceKind::StructuredBuffer || kind == ResourceKind::RWStructuredBuffer;
        }

        bool valid_shader_name(std::string_view name)
        {
            if (name.empty() || name.front() == '/' || name.back() == '/') return false;
            std::size_t segment_start = 0;
            while (segment_start < name.size())
            {
                const std::size_t segment_end = name.find('/', segment_start);
                const std::size_t end = segment_end == std::string_view::npos ? name.size() : segment_end;
                if (end == segment_start) return false;
                const char first = name[segment_start];
                if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_')) return false;
                for (std::size_t index = segment_start + 1; index < end; ++index)
                {
                    const char value = name[index];
                    if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                        (value >= '0' && value <= '9') || value == '_')) return false;
                }
                if (segment_end == std::string_view::npos) break;
                segment_start = segment_end + 1;
            }
            return true;
        }

        bool is_stencil_compare(std::string_view value)
        {
            return is_one_of(value, {"Never", "Less", "Equal", "LessEqual", "Greater", "NotEqual", "GreaterEqual", "Always"});
        }

        bool is_stencil_operation(std::string_view value)
        {
            return is_one_of(value, {"Keep", "Zero", "Replace", "IncrementClamp", "DecrementClamp", "Invert", "IncrementWrap", "DecrementWrap"});
        }

        bool is_blend_factor(std::string_view value)
        {
            return is_one_of(value, {"Zero", "One", "SrcColor", "OneMinusSrcColor", "DstColor", "OneMinusDstColor", "SrcAlpha", "OneMinusSrcAlpha", "DstAlpha", "OneMinusDstAlpha", "ConstantColor", "OneMinusConstantColor", "ConstantAlpha", "OneMinusConstantAlpha", "SrcAlphaSaturate"});
        }

        bool is_blend_operation(std::string_view value)
        {
            return is_one_of(value, {"Add", "Subtract", "ReverseSubtract", "Min", "Max"});
        }

        std::optional<BindingGroup> binding_group_from_name(std::string_view name)
        {
            if (name == "Global") return BindingGroup::Global;
            if (name == "View") return BindingGroup::View;
            if (name == "Pass") return BindingGroup::Pass;
            if (name == "Material") return BindingGroup::Material;
            if (name == "Object") return BindingGroup::Object;
            return std::nullopt;
        }

        std::optional<ShaderStage> stage_from_pragma(std::string_view name)
        {
            if (name == "vertex") return ShaderStage::Vertex;
            if (name == "pixel") return ShaderStage::Pixel;
            if (name == "compute") return ShaderStage::Compute;
            return std::nullopt;
        }

        bool is_valid_pass_state_value(std::string_view state, std::string_view value)
        {
            if (state == "PrimitiveTopology") return is_one_of(value, {"PointList", "LineList", "LineStrip", "TriangleList", "TriangleStrip"});
            if (state == "Cull") return is_one_of(value, {"Off", "Front", "Back"});
            if (state == "FrontFace") return is_one_of(value, {"Clockwise", "CounterClockwise"});
            if (state == "Fill") return is_one_of(value, {"Solid", "Wireframe"});
            if (state == "DepthTest") return is_one_of(value, {"Off", "Never", "Less", "Equal", "LessEqual", "Greater", "NotEqual", "GreaterEqual", "Always"});
            if (state == "DepthWrite") return is_one_of(value, {"Off", "On"});
            if (state == "ColorWrite") return is_one_of(value, {"None", "R", "G", "B", "A", "RG", "RGB", "RGBA"});
            return false;
        }

        bool is_pass_state_name(std::string_view name)
        {
            return is_one_of(name, {
                "PrimitiveTopology", "Cull", "FrontFace", "Fill", "DepthTest",
                "DepthWrite", "Stencil", "Blend", "ColorWrite"});
        }
    }

    bool ParseResult::succeeded() const
    {
        return asset.has_value() && std::none_of(
            diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
                return diagnostic.severity == DiagnosticSeverity::Error;
            });
    }

    ParseResult parse_shader(std::string_view source, std::string path)
    {
        return ShaderParser(source, std::move(path)).parse();
    }

    ShaderParser::ShaderParser(std::string_view source, std::string path)
        : tokenizer(source, std::move(path))
    {
    }

    ParseResult ShaderParser::parse()
    {
        ShaderAsset asset;
        if (!match_identifier("Shader"))
        {
            add_error(DiagnosticCode::UnexpectedToken, peek().location, "Shader asset must begin with 'Shader'.");
        }
        else
        {
            asset.location = peek().location;
        }

        if (const auto name = expect(TokenKind::StringLiteral, "Expected the Shader display name."))
        {
            asset.name = name->text;
            if (!valid_shader_name(asset.name))
            {
                add_error(DiagnosticCode::InvalidShaderName, name->location, "Shader name must contain slash-separated ASCII identifier segments.");
            }
        }
        expect(TokenKind::LeftBrace, "Expected '{' after the Shader name.");

        if (!match_identifier("Version"))
        {
            add_error(DiagnosticCode::InvalidVersion, peek().location, "Shader asset must declare Version 1 first.");
        }
        if (const auto version = expect(TokenKind::Number, "Expected numeric Shader version."))
        {
            if (version->text != "1")
            {
                add_error(DiagnosticCode::InvalidVersion, version->location, "Only Shader asset Version 1 is supported.");
            }
            else
            {
                asset.version = 1;
            }
        }

        bool has_properties = false;
        bool has_resources = false;
        bool has_variants = false;
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            if (match_identifier("Properties"))
            {
                if (has_properties)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location, "Properties may only be declared once.");
                }
                has_properties = true;
                parse_properties(asset);
            }
            else if (match_identifier("Resources"))
            {
                if (has_resources)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location, "Resources may only be declared once.");
                }
                has_resources = true;
                parse_resources(asset);
            }
            else if (match_identifier("Variants"))
            {
                if (has_variants)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location, "Variants may only be declared once.");
                }
                has_variants = true;
                parse_variants(asset);
            }
            else if (match_identifier("HLSLINCLUDE"))
            {
                HlslBlock block;
                parse_hlsl_block(block);
                asset.includes.push_back(std::move(block));
            }
            else if (match_identifier("Pass"))
            {
                parse_pass(asset);
            }
            else
            {
                const Token unexpected = consume();
                add_error(
                    DiagnosticCode::UnexpectedToken,
                    unexpected.location,
                    "Unknown Shader field '" + unexpected.text + "'.");
            }
        }
        expect(TokenKind::RightBrace, "Expected '}' to close the Shader asset.");
        if (!check(TokenKind::EndOfFile))
        {
            add_error(DiagnosticCode::UnexpectedToken, peek().location, "Unexpected tokens after the Shader asset.");
        }
        if (asset.passes.empty())
        {
            add_error(DiagnosticCode::MissingEntryPoint, asset.location, "Shader asset must contain at least one Pass.");
        }

        copy_tokenizer_diagnostics();
        ParseResult result;
        result.asset = std::move(asset);
        result.diagnostics = std::move(parser_diagnostics);
        if (!result.succeeded())
        {
            result.asset.reset();
        }
        return result;
    }

    const Token& ShaderParser::peek()
    {
        if (!lookahead)
        {
            lookahead = tokenizer.next();
            copy_tokenizer_diagnostics();
        }
        return *lookahead;
    }

    Token ShaderParser::consume()
    {
        Token result = peek();
        lookahead.reset();
        return result;
    }

    bool ShaderParser::check(TokenKind kind)
    {
        return peek().kind == kind;
    }

    bool ShaderParser::check_identifier(std::string_view text)
    {
        return peek().kind == TokenKind::Identifier && peek().text == text;
    }

    bool ShaderParser::match(TokenKind kind)
    {
        if (!check(kind))
        {
            return false;
        }
        consume();
        return true;
    }

    bool ShaderParser::match_identifier(std::string_view text)
    {
        if (!check_identifier(text))
        {
            return false;
        }
        consume();
        return true;
    }

    std::optional<Token> ShaderParser::expect(TokenKind kind, std::string message)
    {
        if (check(kind))
        {
            return consume();
        }
        add_error(DiagnosticCode::UnexpectedToken, peek().location, std::move(message));
        return std::nullopt;
    }

    std::optional<Token> ShaderParser::expect_identifier(std::string message)
    {
        return expect(TokenKind::Identifier, std::move(message));
    }

    bool ShaderParser::parse_properties(ShaderAsset& asset)
    {
        if (!expect(TokenKind::LeftBrace, "Expected '{' after Properties."))
        {
            return false;
        }
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            parse_property(asset);
        }
        return expect(TokenKind::RightBrace, "Expected '}' to close Properties.").has_value();
    }

    bool ShaderParser::parse_property(ShaderAsset& asset)
    {
        const auto name = expect_identifier("Expected a property name.");
        if (!name)
        {
            consume();
            return false;
        }
        Property property;
        property.name = name->text;
        property.location = name->location;
        validate_identifier(asset, *name, "property");
        const bool duplicate = contains_name(asset.properties, property.name);
        if (duplicate)
        {
            add_error(DiagnosticCode::DuplicateProperty, name->location, "Duplicate property '" + property.name + "'.");
        }
        expect(TokenKind::LeftParenthesis, "Expected '(' after the property name.");
        if (const auto display = expect(TokenKind::StringLiteral, "Expected a property display name."))
        {
            property.display_name = display->text;
        }
        expect(TokenKind::Comma, "Expected ',' after the property display name.");
        const auto type = expect_identifier("Expected a property type.");
        if (type)
        {
            const auto parsed_type = property_type_from_name(type->text);
            if (!parsed_type)
            {
                add_error(DiagnosticCode::InvalidPropertyType, type->location, "Unknown property type '" + type->text + "'.");
            }
            else
            {
                property.type = *parsed_type;
                if (*parsed_type == PropertyType::Range)
                {
                    expect(TokenKind::LeftParenthesis, "Expected '(' after Range.");
                    property.range_min = parse_number();
                    expect(TokenKind::Comma, "Expected ',' between Range bounds.");
                    property.range_max = parse_number();
                    expect(TokenKind::RightParenthesis, "Expected ')' after Range bounds.");
                    if (property.range_min && property.range_max && *property.range_min > *property.range_max)
                    {
                        add_error(DiagnosticCode::InvalidPropertyType, type->location, "Range minimum cannot exceed its maximum.");
                    }
                }
            }
        }
        expect(TokenKind::RightParenthesis, "Expected ')' after the property declaration.");
        expect(TokenKind::Equal, "Expected '=' before the property default value.");
        parse_default_value(property.default_value);
        if (!duplicate)
        {
            asset.properties.push_back(std::move(property));
        }
        return true;
    }

    bool ShaderParser::parse_resources(ShaderAsset& asset)
    {
        if (!expect(TokenKind::LeftBrace, "Expected '{' after Resources."))
        {
            return false;
        }
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            parse_resource_group(asset);
        }
        return expect(TokenKind::RightBrace, "Expected '}' to close Resources.").has_value();
    }

    bool ShaderParser::parse_resource_group(ShaderAsset& asset)
    {
        const auto group_token = expect_identifier("Expected a resource Binding Group.");
        if (!group_token)
        {
            consume();
            return false;
        }
        const auto group = binding_group_from_name(group_token->text);
        if (!group)
        {
            add_error(DiagnosticCode::InvalidResourceGroup, group_token->location, "Unknown Binding Group '" + group_token->text + "'.");
        }
        expect(TokenKind::LeftBrace, "Expected '{' after the resource Binding Group.");
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            const auto name = expect_identifier("Expected a resource name.");
            if (!name)
            {
                consume();
                continue;
            }
            Resource resource;
            resource.group = group.value_or(BindingGroup::Pass);
            resource.name = name->text;
            resource.location = name->location;
            validate_identifier(asset, *name, "resource");
            const bool duplicate = contains_name(asset.resources, resource.name);
            if (duplicate)
            {
                add_error(DiagnosticCode::DuplicateResource, name->location, "Duplicate resource '" + resource.name + "'.");
            }
            expect(TokenKind::Colon, "Expected ':' after the resource name.");
            parse_resource_type(resource);
            if (match(TokenKind::Equal))
            {
                parse_default_value(resource.default_value);
            }
            if (!duplicate)
            {
                asset.resources.push_back(std::move(resource));
            }
        }
        return expect(TokenKind::RightBrace, "Expected '}' to close the resource Binding Group.").has_value();
    }

    bool ShaderParser::parse_resource_type(Resource& resource)
    {
        const auto type = expect_identifier("Expected a resource type.");
        if (!type)
        {
            return false;
        }
        const auto kind = resource_kind_from_name(type->text);
        if (!kind)
        {
            add_error(DiagnosticCode::InvalidResourceType, type->location, "Unknown resource type '" + type->text + "'.");
            return false;
        }
        resource.kind = *kind;
        const bool has_element = match(TokenKind::LeftAngle);
        if (requires_element_type(*kind) != has_element)
        {
            add_error(
                DiagnosticCode::InvalidResourceType,
                type->location,
                requires_element_type(*kind) ? "Resource type requires an element type." : "Resource type does not accept an element type.");
            if (!has_element)
            {
                return false;
            }
        }
        if (!has_element)
        {
            return true;
        }

        const auto element = expect_identifier("Expected a resource element type.");
        if (element)
        {
            const auto parsed_element = resource_element_type_from_name(element->text);
            if (!parsed_element || (is_matrix(*parsed_element) && !allows_matrix_element(*kind)))
            {
                add_error(DiagnosticCode::InvalidResourceType, element->location, "Resource element type '" + element->text + "' is not supported by this resource kind.");
            }
            else
            {
                resource.element_type = *parsed_element;
            }
        }
        expect(TokenKind::RightAngle, "Expected '>' after the resource element type.");
        return resource.element_type != ResourceElementType::None;
    }

    bool ShaderParser::parse_variants(ShaderAsset& asset)
    {
        if (!expect(TokenKind::LeftBrace, "Expected '{' after Variants."))
        {
            return false;
        }
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            parse_variant(asset);
        }
        return expect(TokenKind::RightBrace, "Expected '}' to close Variants.").has_value();
    }

    bool ShaderParser::parse_variant(ShaderAsset& asset)
    {
        const auto name = expect_identifier("Expected a variant name.");
        if (!name)
        {
            consume();
            return false;
        }
        Variant variant;
        variant.name = name->text;
        variant.location = name->location;
        validate_identifier(asset, *name, "variant");
        const bool duplicate = contains_name(asset.variants, variant.name);
        if (duplicate)
        {
            add_error(DiagnosticCode::DuplicateVariant, name->location, "Duplicate variant '" + variant.name + "'.");
        }
        expect(TokenKind::Colon, "Expected ':' after the variant name.");
        const auto type = expect_identifier("Expected 'bool' or 'enum' as the variant type.");
        if (type && type->text == "bool")
        {
            variant.type = VariantType::Boolean;
            expect(TokenKind::Equal, "Expected '=' before the bool variant default.");
            const auto value = expect_identifier("Expected true or false as the bool variant default.");
            if (value)
            {
                variant.default_value = value->text;
                if (!is_one_of(value->text, {"true", "false"}))
                {
                    add_error(DiagnosticCode::InvalidVariant, value->location, "Bool variant default must be true or false.");
                }
            }
        }
        else if (type && type->text == "enum")
        {
            variant.type = VariantType::Enumeration;
            expect(TokenKind::LeftBrace, "Expected '{' before enum variant options.");
            while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
            {
                const auto option = expect_identifier("Expected an enum variant option.");
                if (option)
                {
                    if (std::find(variant.options.begin(), variant.options.end(), option->text) != variant.options.end())
                    {
                        add_error(DiagnosticCode::InvalidVariant, option->location, "Duplicate enum variant option '" + option->text + "'.");
                    }
                    variant.options.push_back(option->text);
                }
                if (!match(TokenKind::Comma))
                {
                    break;
                }
            }
            expect(TokenKind::RightBrace, "Expected '}' after enum variant options.");
            expect(TokenKind::Equal, "Expected '=' before the enum variant default.");
            if (const auto value = expect_identifier("Expected the enum variant default option."))
            {
                variant.default_value = value->text;
                if (std::find(variant.options.begin(), variant.options.end(), value->text) == variant.options.end())
                {
                    add_error(DiagnosticCode::InvalidVariant, value->location, "Enum variant default is not one of its options.");
                }
            }
        }
        else if (type)
        {
            add_error(DiagnosticCode::InvalidVariant, type->location, "Unknown variant type '" + type->text + "'.");
        }
        if (!duplicate)
        {
            asset.variants.push_back(std::move(variant));
        }
        return true;
    }

    bool ShaderParser::parse_pass(ShaderAsset& asset)
    {
        const auto name = expect(TokenKind::StringLiteral, "Expected the Pass name.");
        ShaderPass pass;
        if (name)
        {
            pass.name = name->text;
            pass.location = name->location;
            if (pass.name.empty())
            {
                add_error(DiagnosticCode::InvalidPassName, name->location, "Pass name cannot be empty.");
            }
            if (contains_name(asset.passes, pass.name))
            {
                add_error(DiagnosticCode::DuplicatePass, name->location, "Duplicate Pass '" + pass.name + "'.");
            }
        }
        expect(TokenKind::LeftBrace, "Expected '{' after the Pass name.");
        bool has_program = false;
        std::unordered_set<std::string> declared_states;
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            if (match_identifier("Requires"))
            {
                if (const auto requirement = expect_identifier("Expected a capability after Requires."))
                {
                    pass.requirements.push_back(requirement->text);
                }
                continue;
            }
            if (match_identifier("HLSLPROGRAM"))
            {
                if (has_program)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location, "Pass may only contain one HLSLPROGRAM block.");
                }
                has_program = true;
                parse_hlsl_block(pass.program);
                continue;
            }

            if (peek().kind == TokenKind::Identifier && is_pass_state_name(peek().text))
            {
                const Token state_name = consume();
                pass.has_explicit_graphics_state = true;
                if (!declared_states.insert(state_name.text).second)
                {
                    add_error(DiagnosticCode::DuplicatePassState, state_name.location, "Pass state '" + state_name.text + "' is declared more than once.");
                }
                if (state_name.text == "Stencil")
                {
                    parse_stencil_state(pass);
                    continue;
                }
                if (state_name.text == "Blend")
                {
                    parse_blend_state(pass);
                    continue;
                }
                const auto value = expect_identifier("Expected a value after Pass state '" + state_name.text + "'.");
                if (value)
                {
                    if (!is_valid_pass_state_value(state_name.text, value->text))
                    {
                        add_error(
                            DiagnosticCode::InvalidPassState,
                            value->location,
                            "Invalid value '" + value->text + "' for Pass state '" + state_name.text + "'.");
                    }
                    if (state_name.text == "PrimitiveTopology") pass.state.primitive_topology = value->text;
                    else if (state_name.text == "Cull") pass.state.cull = value->text;
                    else if (state_name.text == "FrontFace") pass.state.front_face = value->text;
                    else if (state_name.text == "Fill") pass.state.fill = value->text;
                    else if (state_name.text == "DepthTest") pass.state.depth_test = value->text;
                    else if (state_name.text == "DepthWrite") pass.state.depth_write = value->text == "On";
                    else if (state_name.text == "ColorWrite") pass.state.color_write = value->text;
                }
                continue;
            }

            const Token unexpected = consume();
            add_error(DiagnosticCode::InvalidPassState, unexpected.location, "Unknown Pass field '" + unexpected.text + "'.");
        }
        expect(TokenKind::RightBrace, "Expected '}' to close the Pass.");
        if (!has_program)
        {
            add_error(DiagnosticCode::MissingEntryPoint, pass.location, "Pass is missing HLSLPROGRAM.");
        }
        else
        {
            validate_program(pass);
        }
        asset.passes.push_back(std::move(pass));
        return true;
    }

    bool ShaderParser::parse_stencil_state(ShaderPass& pass)
    {
        if (match_identifier("Off"))
        {
            pass.state.stencil = StencilState{};
            return true;
        }
        if (!expect(TokenKind::LeftBrace, "Expected 'Off' or '{' after Stencil."))
        {
            return false;
        }

        bool has_front_and_back = false;
        bool has_front = false;
        bool has_back = false;
        std::unordered_set<std::string> fields;
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            const auto field = expect_identifier("Expected a Stencil field.");
            if (!field)
            {
                consume();
                continue;
            }
            if (!fields.insert(field->text).second)
            {
                add_error(DiagnosticCode::InvalidPassState, field->location, "Stencil field '" + field->text + "' is declared more than once.");
            }
            if (field->text == "ReadMask" || field->text == "WriteMask")
            {
                const auto value = expect(TokenKind::Number, "Expected an integer Stencil mask.");
                if (value)
                {
                    char* end = nullptr;
                    const unsigned long parsed = std::strtoul(value->text.c_str(), &end, 10);
                    if (*end != '\0' || parsed > std::numeric_limits<std::uint8_t>::max())
                    {
                        add_error(DiagnosticCode::InvalidPassState, value->location, "Stencil mask must be an integer from 0 through 255.");
                    }
                    else if (field->text == "ReadMask") pass.state.stencil.read_mask = static_cast<std::uint8_t>(parsed);
                    else pass.state.stencil.write_mask = static_cast<std::uint8_t>(parsed);
                }
                continue;
            }
            if (field->text == "FrontAndBack")
            {
                has_front_and_back = true;
                expect(TokenKind::LeftBrace, "Expected '{' after FrontAndBack.");
                parse_stencil_face(pass.state.stencil.front);
                expect(TokenKind::RightBrace, "Expected '}' after FrontAndBack operations.");
                pass.state.stencil.back = pass.state.stencil.front;
                continue;
            }
            if (field->text == "Front" || field->text == "Back")
            {
                const bool is_front = field->text == "Front";
                has_front = has_front || is_front;
                has_back = has_back || !is_front;
                expect(TokenKind::LeftBrace, "Expected '{' after Stencil face.");
                parse_stencil_face(is_front ? pass.state.stencil.front : pass.state.stencil.back);
                expect(TokenKind::RightBrace, "Expected '}' after Stencil face operations.");
                continue;
            }
            add_error(DiagnosticCode::InvalidPassState, field->location, "Unknown Stencil field '" + field->text + "'.");
        }
        expect(TokenKind::RightBrace, "Expected '}' to close Stencil state.");
        if (has_front_and_back == (has_front || has_back) || (!has_front_and_back && !(has_front && has_back)))
        {
            add_error(DiagnosticCode::InvalidPassState, pass.location, "Stencil requires FrontAndBack or both Front and Back.");
            return false;
        }
        pass.state.stencil.mode = has_front_and_back ? StencilMode::FrontAndBack : StencilMode::SeparateFaces;
        return true;
    }

    bool ShaderParser::parse_stencil_face(StencilFaceState& face)
    {
        const char* names[] = {"Compare", "Fail", "DepthFail", "Pass"};
        std::string* values[] = {&face.compare, &face.fail, &face.depth_fail, &face.pass};
        for (std::size_t index = 0; index < 4; ++index)
        {
            if (!match_identifier(names[index]))
            {
                add_error(DiagnosticCode::InvalidPassState, peek().location, "Expected Stencil operation field '" + std::string(names[index]) + "'.");
                return false;
            }
            const auto value = expect_identifier("Expected a Stencil operation value.");
            if (!value) return false;
            const bool valid = index == 0 ? is_stencil_compare(value->text) : is_stencil_operation(value->text);
            if (!valid)
            {
                add_error(DiagnosticCode::InvalidPassState, value->location, "Invalid Stencil operation value '" + value->text + "'.");
            }
            *values[index] = value->text;
        }
        return true;
    }

    bool ShaderParser::parse_blend_state(ShaderPass& pass)
    {
        if (match_identifier("Off"))
        {
            pass.state.blend = BlendState{};
            return true;
        }
        if (!expect(TokenKind::LeftBrace, "Expected 'Off' or '{' after Blend.")) return false;
        pass.state.blend.enabled = true;
        struct BlendLine
        {
            const char* name;
            std::string* source;
            std::string* destination;
            std::string* operation;
        };
        BlendLine lines[] = {
            {"Color", &pass.state.blend.source_color, &pass.state.blend.destination_color, &pass.state.blend.color_operation},
            {"Alpha", &pass.state.blend.source_alpha, &pass.state.blend.destination_alpha, &pass.state.blend.alpha_operation}};
        for (BlendLine& line : lines)
        {
            if (!match_identifier(line.name))
            {
                add_error(DiagnosticCode::InvalidPassState, peek().location, "Expected Blend field '" + std::string(line.name) + "'.");
                break;
            }
            const auto source = expect_identifier("Expected source Blend factor.");
            const auto destination = expect_identifier("Expected destination Blend factor.");
            const auto operation = expect_identifier("Expected Blend operation.");
            if (source)
            {
                if (!is_blend_factor(source->text)) add_error(DiagnosticCode::InvalidPassState, source->location, "Invalid Blend factor '" + source->text + "'.");
                *line.source = source->text;
            }
            if (destination)
            {
                if (!is_blend_factor(destination->text)) add_error(DiagnosticCode::InvalidPassState, destination->location, "Invalid Blend factor '" + destination->text + "'.");
                *line.destination = destination->text;
            }
            if (operation)
            {
                if (!is_blend_operation(operation->text)) add_error(DiagnosticCode::InvalidPassState, operation->location, "Invalid Blend operation '" + operation->text + "'.");
                *line.operation = operation->text;
            }
        }
        return expect(TokenKind::RightBrace, "Expected '}' to close Blend state.").has_value();
    }

    bool ShaderParser::parse_hlsl_block(HlslBlock& block)
    {
        if (lookahead)
        {
            add_error(DiagnosticCode::UnexpectedToken, lookahead->location, "Internal parser state prevented raw HLSL capture.");
            return false;
        }
        RawBlock raw = tokenizer.consume_raw_block("ENDHLSL");
        block.source = std::move(raw.text);
        block.location = std::move(raw.location);
        copy_tokenizer_diagnostics();
        extract_pragmas(block);
        return raw.terminated;
    }

    bool ShaderParser::parse_default_value(DefaultValue& value)
    {
        value.location = peek().location;
        if (check(TokenKind::StringLiteral))
        {
            value.kind = DefaultValueKind::String;
            value.text = consume().text;
            return true;
        }
        if (check(TokenKind::Identifier))
        {
            value.kind = DefaultValueKind::Identifier;
            value.text = consume().text;
            return true;
        }
        if (match(TokenKind::LeftParenthesis))
        {
            value.kind = DefaultValueKind::Numbers;
            do
            {
                const auto number = parse_number();
                if (!number)
                {
                    break;
                }
                value.numbers.push_back(*number);
            } while (match(TokenKind::Comma));
            expect(TokenKind::RightParenthesis, "Expected ')' after the default value list.");
            return !value.numbers.empty();
        }
        if (check(TokenKind::Number) || check(TokenKind::Minus) || check(TokenKind::Plus))
        {
            value.kind = DefaultValueKind::Numbers;
            if (const auto number = parse_number())
            {
                value.numbers.push_back(*number);
                return true;
            }
        }
        add_error(DiagnosticCode::InvalidDefaultValue, peek().location, "Expected a property or resource default value.");
        return false;
    }

    std::optional<double> ShaderParser::parse_number()
    {
        std::string text;
        SourceLocation start = peek().location;
        if (check(TokenKind::Minus) || check(TokenKind::Plus))
        {
            text = consume().text;
        }
        const auto token = expect(TokenKind::Number, "Expected a numeric value.");
        if (!token)
        {
            return std::nullopt;
        }
        text += token->text;
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0')
        {
            add_error(DiagnosticCode::InvalidDefaultValue, start, "Invalid numeric value '" + text + "'.");
            return std::nullopt;
        }
        return value;
    }

    void ShaderParser::extract_pragmas(HlslBlock& block)
    {
        std::istringstream source(block.source);
        std::string line_text;
        std::size_t line_number = block.location.line;
        while (std::getline(source, line_text))
        {
            const std::size_t first = line_text.find_first_not_of(" \t");
            if (first != std::string::npos && line_text.compare(first, 7, "#pragma") == 0 &&
                line_text.size() > first + 7 &&
                (line_text[first + 7] == ' ' || line_text[first + 7] == '\t'))
            {
                std::istringstream pragma(line_text.substr(first + 7));
                std::string stage_name;
                std::string entry_name;
                std::string trailing;
                pragma >> stage_name >> entry_name >> trailing;
                const SourceLocation location{
                    block.location.path,
                    block.location.offset,
                    line_number,
                    first + 1};
                const auto stage = stage_from_pragma(stage_name);
                if (!stage || entry_name.empty() || !trailing.empty())
                {
                    add_error(DiagnosticCode::UnknownPragma, location, "Expected '#pragma vertex|pixel|compute entry_name'.");
                }
                else
                {
                    const bool duplicate = std::any_of(block.entry_points.begin(), block.entry_points.end(), [&](const EntryPoint& entry) {
                        return entry.stage == *stage;
                    });
                    if (duplicate)
                    {
                        add_error(DiagnosticCode::DuplicateEntryPoint, location, "Shader stage pragma is declared more than once.");
                    }
                    else
                    {
                        block.entry_points.push_back(EntryPoint{*stage, std::move(entry_name), location});
                    }
                }
            }
            ++line_number;
        }
    }

    void ShaderParser::validate_program(ShaderPass& pass)
    {
        const bool has_vertex = std::any_of(pass.program.entry_points.begin(), pass.program.entry_points.end(), [](const EntryPoint& entry) {
            return entry.stage == ShaderStage::Vertex;
        });
        const bool has_pixel = std::any_of(pass.program.entry_points.begin(), pass.program.entry_points.end(), [](const EntryPoint& entry) {
            return entry.stage == ShaderStage::Pixel;
        });
        const bool has_compute = std::any_of(pass.program.entry_points.begin(), pass.program.entry_points.end(), [](const EntryPoint& entry) {
            return entry.stage == ShaderStage::Compute;
        });
        if (has_compute && (has_vertex || has_pixel))
        {
            add_error(DiagnosticCode::MixedProgramStages, pass.program.location, "A Pass cannot mix compute and graphics entry points.");
        }
        else if (has_compute)
        {
            if (pass.has_explicit_graphics_state)
            {
                add_error(DiagnosticCode::InvalidPassState, pass.location, "Compute Pass cannot declare graphics pipeline state.");
            }
            return;
        }
        else if (!has_vertex)
        {
            add_error(DiagnosticCode::MissingEntryPoint, pass.program.location, "Graphics Pass requires a vertex pragma; pixel is optional.");
        }
    }

    void ShaderParser::validate_identifier(ShaderAsset& asset, const Token& token, std::string_view category)
    {
        if (token.text.rfind("toy3d_", 0) == 0 || token.text.rfind("TOY3D_", 0) == 0)
        {
            add_error(DiagnosticCode::ReservedIdentifier, token.location, "Identifier '" + token.text + "' uses a reserved Toy3d prefix.");
        }
        const bool conflict = (category != "property" && contains_name(asset.properties, token.text)) ||
            (category != "resource" && contains_name(asset.resources, token.text)) ||
            (category != "variant" && contains_name(asset.variants, token.text));
        if (conflict)
        {
            add_error(DiagnosticCode::IdentifierConflict, token.location, "The " + std::string(category) + " identifier '" + token.text + "' conflicts with another generated HLSL identifier.");
        }
    }

    void ShaderParser::add_error(DiagnosticCode code, const SourceLocation& at, std::string message)
    {
        parser_diagnostics.push_back(Diagnostic{DiagnosticSeverity::Error, code, at, std::move(message)});
    }

    void ShaderParser::copy_tokenizer_diagnostics()
    {
        const auto& diagnostics = tokenizer.diagnostics();
        while (copied_tokenizer_diagnostics < diagnostics.size())
        {
            parser_diagnostics.push_back(diagnostics[copied_tokenizer_diagnostics]);
            ++copied_tokenizer_diagnostics;
        }
    }
}
