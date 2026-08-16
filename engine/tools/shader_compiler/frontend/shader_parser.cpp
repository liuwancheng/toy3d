#include "frontend/shader_parser.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
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
            return std::nullopt;
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
            if (state == "DepthWrite" || state == "Stencil" || state == "Blend") return is_one_of(value, {"Off", "On"});
            if (state == "ColorWrite") return is_one_of(value, {"None", "R", "RG", "RGB", "RGBA"});
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
            const bool duplicate = contains_name(asset.resources, resource.name);
            if (duplicate)
            {
                add_error(DiagnosticCode::DuplicateResource, name->location, "Duplicate resource '" + resource.name + "'.");
            }
            expect(TokenKind::Colon, "Expected ':' after the resource name.");
            if (const auto type = expect_identifier("Expected a resource type."))
            {
                resource.type = type->text;
                if (match(TokenKind::LeftAngle))
                {
                    resource.type += '<';
                    int depth = 1;
                    while (depth > 0 && !check(TokenKind::EndOfFile))
                    {
                        const Token part = consume();
                        if (part.kind == TokenKind::LeftAngle) ++depth;
                        if (part.kind == TokenKind::RightAngle) --depth;
                        resource.type += part.text;
                    }
                }
            }
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
            if (contains_name(asset.passes, pass.name))
            {
                add_error(DiagnosticCode::DuplicatePass, name->location, "Duplicate Pass '" + pass.name + "'.");
            }
        }
        expect(TokenKind::LeftBrace, "Expected '{' after the Pass name.");
        bool has_program = false;
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
                    const bool duplicate_state = std::any_of(pass.states.begin(), pass.states.end(), [&](const PassState& pass_state) {
                        return pass_state.name == state_name.text;
                    });
                    if (duplicate_state)
                    {
                        add_error(DiagnosticCode::InvalidPassState, state_name.location, "Pass state '" + state_name.text + "' is declared more than once.");
                    }
                    pass.states.push_back(PassState{state_name.text, value->text, state_name.location});
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
            return;
        }
        else if (!has_vertex || !has_pixel)
        {
            add_error(DiagnosticCode::MissingEntryPoint, pass.program.location, "Graphics Pass requires both vertex and pixel pragmas.");
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
