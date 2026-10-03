#include "frontend/shader_parser.h"

#include "shader/builtin_shader_parameters.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace toy3d::shader
{
    // Parser helpers use string_view for non-owning token-name comparisons and
    // optional when a grammar conversion has no valid value to return.
    namespace
    {
        template <typename Collection> bool contains_name(const Collection& values, const std::string& name)
        {
            return std::any_of(values.begin(), values.end(),
                               [&](const auto& value)
                               {
                                   return value.name == name;
                               });
        }

        bool is_one_of(std::string_view value, std::initializer_list<std::string_view> choices)
        {
            return std::find(choices.begin(), choices.end(), value) != choices.end();
        }

        std::optional<PropertyType> property_type_from_name(std::string_view name)
        {
            if (name == "Float")
            {
                return PropertyType::Float;
            }
            if (name == "Float2")
            {
                return PropertyType::Float2;
            }
            if (name == "Float3")
            {
                return PropertyType::Float3;
            }
            if (name == "Float4")
            {
                return PropertyType::Float4;
            }
            if (name == "Color")
            {
                return PropertyType::Color;
            }
            if (name == "Matrix4x4")
            {
                return PropertyType::Matrix4x4;
            }
            if (name == "Range")
            {
                return PropertyType::Range;
            }
            if (name == "Texture2D")
            {
                return PropertyType::Texture2D;
            }
            if (name == "TextureCube")
            {
                return PropertyType::TextureCube;
            }
            if (name == "Sampler")
            {
                return PropertyType::Sampler;
            }
            if (name == "ComparisonSampler")
            {
                return PropertyType::ComparisonSampler;
            }
            return std::nullopt;
        }

        std::optional<ShaderValueType> parameter_type_from_name(std::string_view name)
        {
            if (name == "Float")
            {
                return ShaderValueType::Float32;
            }
            if (name == "Float2")
            {
                return ShaderValueType::Float32x2;
            }
            if (name == "Float3")
            {
                return ShaderValueType::Float32x3;
            }
            if (name == "Float4")
            {
                return ShaderValueType::Float32x4;
            }
            if (name == "Float4x4")
            {
                return ShaderValueType::Float32x4x4;
            }
            return std::nullopt;
        }

        std::optional<ResourceKind> resource_kind_from_name(std::string_view name)
        {
            if (name == "Texture2D")
            {
                return ResourceKind::Texture2D;
            }
            if (name == "Texture2DArray")
            {
                return ResourceKind::Texture2DArray;
            }
            if (name == "Texture3D")
            {
                return ResourceKind::Texture3D;
            }
            if (name == "TextureCube")
            {
                return ResourceKind::TextureCube;
            }
            if (name == "Texture2DMS")
            {
                return ResourceKind::Texture2DMS;
            }
            if (name == "Sampler")
            {
                return ResourceKind::Sampler;
            }
            if (name == "ComparisonSampler")
            {
                return ResourceKind::ComparisonSampler;
            }
            if (name == "Buffer")
            {
                return ResourceKind::Buffer;
            }
            if (name == "ByteAddressBuffer")
            {
                return ResourceKind::ByteAddressBuffer;
            }
            if (name == "StructuredBuffer")
            {
                return ResourceKind::StructuredBuffer;
            }
            if (name == "RWBuffer")
            {
                return ResourceKind::RWBuffer;
            }
            if (name == "RWByteAddressBuffer")
            {
                return ResourceKind::RWByteAddressBuffer;
            }
            if (name == "RWStructuredBuffer")
            {
                return ResourceKind::RWStructuredBuffer;
            }
            if (name == "RWTexture2D")
            {
                return ResourceKind::RWTexture2D;
            }
            if (name == "RWTexture2DArray")
            {
                return ResourceKind::RWTexture2DArray;
            }
            if (name == "RWTexture3D")
            {
                return ResourceKind::RWTexture3D;
            }
            return std::nullopt;
        }

        std::optional<ResourceElementType> resource_element_type_from_name(std::string_view name)
        {
            if (name == "Float")
            {
                return ResourceElementType::Float;
            }
            if (name == "Float2")
            {
                return ResourceElementType::Float2;
            }
            if (name == "Float3")
            {
                return ResourceElementType::Float3;
            }
            if (name == "Float4")
            {
                return ResourceElementType::Float4;
            }
            if (name == "Int")
            {
                return ResourceElementType::Int;
            }
            if (name == "Int2")
            {
                return ResourceElementType::Int2;
            }
            if (name == "Int3")
            {
                return ResourceElementType::Int3;
            }
            if (name == "Int4")
            {
                return ResourceElementType::Int4;
            }
            if (name == "UInt")
            {
                return ResourceElementType::UInt;
            }
            if (name == "UInt2")
            {
                return ResourceElementType::UInt2;
            }
            if (name == "UInt3")
            {
                return ResourceElementType::UInt3;
            }
            if (name == "UInt4")
            {
                return ResourceElementType::UInt4;
            }
            if (name == "Float2x2")
            {
                return ResourceElementType::Float2x2;
            }
            if (name == "Float2x3")
            {
                return ResourceElementType::Float2x3;
            }
            if (name == "Float2x4")
            {
                return ResourceElementType::Float2x4;
            }
            if (name == "Float3x2")
            {
                return ResourceElementType::Float3x2;
            }
            if (name == "Float3x3")
            {
                return ResourceElementType::Float3x3;
            }
            if (name == "Float3x4")
            {
                return ResourceElementType::Float3x4;
            }
            if (name == "Float4x2")
            {
                return ResourceElementType::Float4x2;
            }
            if (name == "Float4x3")
            {
                return ResourceElementType::Float4x3;
            }
            if (name == "Float4x4")
            {
                return ResourceElementType::Float4x4;
            }
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

        bool is_stencil_compare(std::string_view value)
        {
            return is_one_of(value,
                             {"Never", "Less", "Equal", "LessEqual", "Greater", "NotEqual", "GreaterEqual", "Always"});
        }

        bool is_stencil_operation(std::string_view value)
        {
            return is_one_of(value, {"Keep", "Zero", "Replace", "IncrementClamp", "DecrementClamp", "Invert",
                                     "IncrementWrap", "DecrementWrap"});
        }

        bool is_blend_factor(std::string_view value)
        {
            return is_one_of(value, {"Zero", "One", "SrcColor", "OneMinusSrcColor", "DstColor", "OneMinusDstColor",
                                     "SrcAlpha", "OneMinusSrcAlpha", "DstAlpha", "OneMinusDstAlpha", "ConstantColor",
                                     "OneMinusConstantColor", "SrcAlphaSaturate"});
        }

        bool is_blend_operation(std::string_view value)
        {
            return is_one_of(value, {"Add", "Subtract", "ReverseSubtract", "Min", "Max"});
        }

        std::optional<BindingGroup> binding_group_from_name(std::string_view name)
        {
            if (name == "Global")
            {
                return BindingGroup::Global;
            }
            if (name == "View")
            {
                return BindingGroup::View;
            }
            if (name == "Pass")
            {
                return BindingGroup::Pass;
            }
            if (name == "Material")
            {
                return BindingGroup::Material;
            }
            if (name == "Object")
            {
                return BindingGroup::Object;
            }
            return std::nullopt;
        }

        std::optional<ShaderStage> stage_from_pragma(std::string_view name)
        {
            if (name == "vertex")
            {
                return ShaderStage::Vertex;
            }
            if (name == "pixel")
            {
                return ShaderStage::Pixel;
            }
            if (name == "compute")
            {
                return ShaderStage::Compute;
            }
            return std::nullopt;
        }

        bool is_valid_pass_state_value(std::string_view state, std::string_view value)
        {
            if (state == "PrimitiveTopology")
            {
                return is_one_of(value, {"PointList", "LineList", "LineStrip", "TriangleList", "TriangleStrip"});
            }
            if (state == "Cull")
            {
                return is_one_of(value, {"Off", "Front", "Back"});
            }
            if (state == "FrontFace")
            {
                return is_one_of(value, {"Clockwise", "CounterClockwise"});
            }
            if (state == "Fill")
            {
                return is_one_of(value, {"Solid", "Wireframe"});
            }
            if (state == "DepthTest")
            {
                return is_one_of(value, {"Off", "Never", "Less", "Equal", "LessEqual", "Greater", "NotEqual",
                                         "GreaterEqual", "Always"});
            }
            if (state == "DepthWrite")
            {
                return is_one_of(value, {"Off", "On"});
            }
            if (state == "ColorWrite")
            {
                return is_one_of(value, {"None", "R", "G", "B", "A", "RG", "RGB", "RGBA"});
            }
            return false;
        }

        // These parser conversions use string_view because token text is only
        // inspected while producing the owned normalized enum state.
        ShaderGraphicsPassState::PrimitiveTopology primitive_topology_from_name(std::string_view value)
        {
            if (value == "PointList")
            {
                return ShaderGraphicsPassState::PrimitiveTopology::PointList;
            }
            if (value == "LineList")
            {
                return ShaderGraphicsPassState::PrimitiveTopology::LineList;
            }
            if (value == "LineStrip")
            {
                return ShaderGraphicsPassState::PrimitiveTopology::LineStrip;
            }
            if (value == "TriangleStrip")
            {
                return ShaderGraphicsPassState::PrimitiveTopology::TriangleStrip;
            }
            return ShaderGraphicsPassState::PrimitiveTopology::TriangleList;
        }

        ShaderGraphicsPassState::CullMode cull_mode_from_name(std::string_view value)
        {
            if (value == "Off")
            {
                return ShaderGraphicsPassState::CullMode::None;
            }
            if (value == "Front")
            {
                return ShaderGraphicsPassState::CullMode::Front;
            }
            return ShaderGraphicsPassState::CullMode::Back;
        }

        ShaderGraphicsPassState::CompareOperation compare_operation_from_name(std::string_view value)
        {
            if (value == "Never")
            {
                return ShaderGraphicsPassState::CompareOperation::Never;
            }
            if (value == "Less")
            {
                return ShaderGraphicsPassState::CompareOperation::Less;
            }
            if (value == "Equal")
            {
                return ShaderGraphicsPassState::CompareOperation::Equal;
            }
            if (value == "LessEqual")
            {
                return ShaderGraphicsPassState::CompareOperation::LessEqual;
            }
            if (value == "Greater")
            {
                return ShaderGraphicsPassState::CompareOperation::Greater;
            }
            if (value == "NotEqual")
            {
                return ShaderGraphicsPassState::CompareOperation::NotEqual;
            }
            if (value == "GreaterEqual")
            {
                return ShaderGraphicsPassState::CompareOperation::GreaterEqual;
            }
            return ShaderGraphicsPassState::CompareOperation::Always;
        }

        ShaderGraphicsPassState::StencilOperation stencil_operation_from_name(std::string_view value)
        {
            if (value == "Zero")
            {
                return ShaderGraphicsPassState::StencilOperation::Zero;
            }
            if (value == "Replace")
            {
                return ShaderGraphicsPassState::StencilOperation::Replace;
            }
            if (value == "IncrementClamp")
            {
                return ShaderGraphicsPassState::StencilOperation::IncrementClamp;
            }
            if (value == "DecrementClamp")
            {
                return ShaderGraphicsPassState::StencilOperation::DecrementClamp;
            }
            if (value == "Invert")
            {
                return ShaderGraphicsPassState::StencilOperation::Invert;
            }
            if (value == "IncrementWrap")
            {
                return ShaderGraphicsPassState::StencilOperation::IncrementWrap;
            }
            if (value == "DecrementWrap")
            {
                return ShaderGraphicsPassState::StencilOperation::DecrementWrap;
            }
            return ShaderGraphicsPassState::StencilOperation::Keep;
        }

        ShaderGraphicsPassState::BlendFactor blend_factor_from_name(std::string_view value)
        {
            if (value == "Zero")
            {
                return ShaderGraphicsPassState::BlendFactor::Zero;
            }
            if (value == "SrcColor")
            {
                return ShaderGraphicsPassState::BlendFactor::SourceColor;
            }
            if (value == "OneMinusSrcColor")
            {
                return ShaderGraphicsPassState::BlendFactor::OneMinusSourceColor;
            }
            if (value == "DstColor")
            {
                return ShaderGraphicsPassState::BlendFactor::DestinationColor;
            }
            if (value == "OneMinusDstColor")
            {
                return ShaderGraphicsPassState::BlendFactor::OneMinusDestinationColor;
            }
            if (value == "SrcAlpha")
            {
                return ShaderGraphicsPassState::BlendFactor::SourceAlpha;
            }
            if (value == "OneMinusSrcAlpha")
            {
                return ShaderGraphicsPassState::BlendFactor::OneMinusSourceAlpha;
            }
            if (value == "DstAlpha")
            {
                return ShaderGraphicsPassState::BlendFactor::DestinationAlpha;
            }
            if (value == "OneMinusDstAlpha")
            {
                return ShaderGraphicsPassState::BlendFactor::OneMinusDestinationAlpha;
            }
            if (value == "ConstantColor")
            {
                return ShaderGraphicsPassState::BlendFactor::ConstantColor;
            }
            if (value == "OneMinusConstantColor")
            {
                return ShaderGraphicsPassState::BlendFactor::OneMinusConstantColor;
            }
            if (value == "SrcAlphaSaturate")
            {
                return ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate;
            }
            return ShaderGraphicsPassState::BlendFactor::One;
        }

        ShaderGraphicsPassState::BlendOperation blend_operation_from_name(std::string_view value)
        {
            if (value == "Subtract")
            {
                return ShaderGraphicsPassState::BlendOperation::Subtract;
            }
            if (value == "ReverseSubtract")
            {
                return ShaderGraphicsPassState::BlendOperation::ReverseSubtract;
            }
            if (value == "Min")
            {
                return ShaderGraphicsPassState::BlendOperation::Minimum;
            }
            if (value == "Max")
            {
                return ShaderGraphicsPassState::BlendOperation::Maximum;
            }
            return ShaderGraphicsPassState::BlendOperation::Add;
        }

        ShaderGraphicsPassState::ColorWriteMask color_write_mask_from_name(std::string_view value)
        {
            if (value == "None")
            {
                return ShaderGraphicsPassState::ColorWriteMask::None;
            }
            if (value == "R")
            {
                return ShaderGraphicsPassState::ColorWriteMask::Red;
            }
            if (value == "G")
            {
                return ShaderGraphicsPassState::ColorWriteMask::Green;
            }
            if (value == "B")
            {
                return ShaderGraphicsPassState::ColorWriteMask::Blue;
            }
            if (value == "A")
            {
                return ShaderGraphicsPassState::ColorWriteMask::Alpha;
            }
            if (value == "RG")
            {
                return ShaderGraphicsPassState::ColorWriteMask::RedGreen;
            }
            if (value == "RGB")
            {
                return ShaderGraphicsPassState::ColorWriteMask::RedGreenBlue;
            }
            return ShaderGraphicsPassState::ColorWriteMask::All;
        }

        bool is_pass_state_name(std::string_view name)
        {
            return is_one_of(name, {"PrimitiveTopology", "Cull", "FrontFace", "Fill", "DepthTest", "DepthWrite",
                                    "Stencil", "Blend", "ColorWrite"});
        }
    } // namespace

    bool ParseResult::succeeded() const
    {
        return asset.has_value() && std::none_of(diagnostics.begin(), diagnostics.end(),
                                                 [](const Diagnostic& diagnostic)
                                                 {
                                                     return diagnostic.severity == DiagnosticSeverity::Error;
                                                 });
    }

    ParseResult parse_shader(std::string_view source, std::string path)
    {
        return ShaderParser(source, std::move(path)).parse();
    }

    ShaderParser::ShaderParser(std::string_view source, std::string path) : tokenizer(source, std::move(path))
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
            if (!valid_shader_source_name(asset.name))
            {
                add_error(DiagnosticCode::InvalidShaderName, name->location,
                          "Shader name must contain slash-separated ASCII identifier segments within 256 bytes.");
            }
        }
        expect(TokenKind::LeftBrace, "Expected '{' after the Shader name.");

        if (!match_identifier("Version"))
        {
            add_error(DiagnosticCode::InvalidVersion, peek().location, "Shader asset must declare Version 2 first.");
        }
        if (const auto version = expect(TokenKind::Number, "Expected numeric Shader version."))
        {
            if (version->text != "2")
            {
                add_error(DiagnosticCode::InvalidVersion, version->location,
                          "Only Shader asset Version 2 is supported. Rewrite the source using the v2 protocol.");
            }
            else
            {
                asset.version = 2;
            }
        }

        if (!match_identifier("Usage"))
        {
            add_error(DiagnosticCode::InvalidShaderName, peek().location, "Version 2 requires an explicit Usage.");
        }
        else if (const auto usage = expect_identifier("Expected Global, Material or MeshPass after Usage."))
        {
            if (usage->text == "Material")
            {
                asset.usage = ShaderUsage::Material;
            }
            else if (usage->text == "MeshPass")
            {
                asset.usage = ShaderUsage::MeshPass;
            }
            else if (usage->text != "Global")
            {
                add_error(DiagnosticCode::InvalidShaderName, usage->location, "Unknown Shader Usage.");
            }
        }
        if (asset.usage != ShaderUsage::Global)
        {
            if (!match_identifier("Geometry"))
            {
                add_error(DiagnosticCode::InvalidShaderName, peek().location,
                          "Mesh sources require explicit Geometry.");
            }
            else if (const auto geometry = expect_identifier("Expected Standard or Custom after Geometry."))
            {
                if (geometry->text != "Custom" && geometry->text != "Standard")
                {
                    add_error(DiagnosticCode::InvalidShaderName, geometry->location, "Unknown geometry mode.");
                }
                asset.geometry =
                    geometry->text == "Standard" ? ShaderGeometryMode::Standard : ShaderGeometryMode::Custom;
            }
            if (!match_identifier("VertexFactories"))
            {
                add_error(DiagnosticCode::InvalidShaderName, peek().location,
                          "Mesh sources require explicit VertexFactories.");
            }
            else
            {
                parse_vertex_factories(asset);
            }
        }

        bool has_properties = false;
        bool has_parameters = false;
        bool has_resources = false;
        bool has_variants = false;
        bool has_features = false;
        bool has_supported_when = false;
        bool has_surface_inputs = false;
        bool has_geometry_requirements = false;
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            if (match_identifier("Properties"))
            {
                if (has_properties)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location,
                              "Properties may only be declared once.");
                }
                has_properties = true;
                parse_properties(asset);
            }
            else if (match_identifier("Parameters"))
            {
                if (asset.usage == ShaderUsage::Material)
                {
                    add_error(DiagnosticCode::InvalidParameterGroup, peek().location,
                              "Material Pass parameters are engine-owned; author values belong in Properties.");
                }
                if (has_parameters)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location,
                              "Parameters may only be declared once.");
                }
                has_parameters = true;
                parse_parameters(asset);
            }
            else if (match_identifier("Resources"))
            {
                if (asset.usage == ShaderUsage::Material)
                {
                    add_error(DiagnosticCode::InvalidParameterGroup, peek().location,
                              "Material Pass resources are engine-owned; declare Features and Material Properties.");
                }
                if (has_resources)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location,
                              "Resources may only be declared once.");
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
            else if (match_identifier("Features"))
            {
                if (has_features || asset.usage != ShaderUsage::Material)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location,
                              "Features are declared once on Material sources.");
                }
                has_features = true;
                parse_features(asset);
            }
            else if (match_identifier("SurfaceInputs"))
            {
                if (has_surface_inputs || asset.geometry != ShaderGeometryMode::Standard)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location,
                              "SurfaceInputs occur once on a Standard source.");
                }
                has_surface_inputs = true;
                expect(TokenKind::LeftBrace, "Expected '{' after SurfaceInputs.");
                while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
                {
                    const Token input = consume();
                    if (input.kind != TokenKind::Identifier || input.text != "Tangent" || asset.standard_tangent_input)
                    {
                        add_error(DiagnosticCode::InvalidShaderName, input.location,
                                  "Unknown or duplicate SurfaceInput; supported input is Tangent.");
                    }
                    asset.standard_tangent_input = true;
                }
                expect(TokenKind::RightBrace, "Expected '}' after SurfaceInputs.");
            }
            else if (match_identifier("GeometryRequirements"))
            {
                if (has_geometry_requirements || asset.usage == ShaderUsage::Global)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location,
                              "GeometryRequirements occur once on a mesh source.");
                }
                has_geometry_requirements = true;
                expect(TokenKind::LeftBrace, "Expected '{' after GeometryRequirements.");
                while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
                {
                    const Token requirement = consume();
                    if (requirement.kind != TokenKind::Identifier || requirement.text != "TangentFrame" ||
                        asset.declares_tangent_frame)
                    {
                        add_error(DiagnosticCode::InvalidShaderName, requirement.location,
                                  "Unknown or duplicate GeometryRequirement; supported requirement is TangentFrame.");
                    }
                    asset.declares_tangent_frame = true;
                    if (match_identifier("When"))
                    {
                        parse_static_condition(asset.tangent_frame_when);
                    }
                }
                expect(TokenKind::RightBrace, "Expected '}' after GeometryRequirements.");
            }
            else if (match_identifier("SupportedWhen"))
            {
                if (has_supported_when)
                {
                    add_error(DiagnosticCode::DuplicateSection, peek().location,
                              "SupportedWhen may only be declared once.");
                }
                has_supported_when = true;
                parse_static_condition(asset.supported_when);
            }
            else if (match_identifier("HLSLINCLUDE"))
            {
                HlslBlock block;
                parse_hlsl_block(block);
                if (!block.entry_points.empty())
                {
                    add_error(DiagnosticCode::UnknownPragma, block.location,
                              "HLSLINCLUDE cannot declare stage entries.");
                }
                asset.includes.push_back(std::move(block));
            }
            else if (match_identifier("Pass"))
            {
                parse_pass(asset);
            }
            else
            {
                const Token unexpected = consume();
                add_error(DiagnosticCode::UnexpectedToken, unexpected.location,
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
            add_error(DiagnosticCode::MissingEntryPoint, asset.location,
                      "Shader asset must contain at least one Pass.");
        }
        if (asset.declares_tangent_frame && asset.geometry == ShaderGeometryMode::Standard &&
            !asset.standard_tangent_input)
        {
            add_error(DiagnosticCode::InvalidShaderName, asset.location,
                      "Standard TangentFrame requires SurfaceInputs { Tangent }.");
        }
        bool has_forward = false;
        std::unordered_set<std::uint32_t> roles;
        for (const ShaderPass& pass : asset.passes)
        {
            ShaderProgramContract contract;
            contract.usage = asset.usage;
            contract.geometry = asset.geometry;
            contract.surface_mode = asset.geometry == ShaderGeometryMode::Standard ? ShaderSurfaceMode::Opaque
                                                                                   : ShaderSurfaceMode::Explicit;
            contract.role = pass.role;
            contract.vertex_factory_support = asset.vertex_factory_support;
            contract.vertex_factory =
                asset.usage == ShaderUsage::Global
                    ? VertexFactoryType::None
                    : (supports_vertex_factory(asset.vertex_factory_support, VertexFactoryType::Local)
                           ? VertexFactoryType::Local
                           : VertexFactoryType::GPUSkin);
            std::string error;
            if (!validate_shader_program_contract(contract, error))
            {
                add_error(DiagnosticCode::InvalidPassName, pass.location, error);
            }
            if (asset.usage != ShaderUsage::Global && !roles.insert(static_cast<std::uint32_t>(pass.role)).second)
            {
                add_error(DiagnosticCode::DuplicatePass, pass.location, "A mesh role may only be implemented once.");
            }
            if (asset.usage != ShaderUsage::Global && pass.state.blend.enabled)
            {
                add_error(DiagnosticCode::InvalidPassState, pass.location,
                          "Mesh shaders do not support transparent blending.");
            }
            has_forward = has_forward || pass.role == ShaderPassRole::Forward;
        }
        if (asset.usage == ShaderUsage::Material && !has_forward)
        {
            add_error(DiagnosticCode::InvalidPassName, asset.location, "Material requires a Forward role.");
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
                add_error(DiagnosticCode::InvalidPropertyType, type->location,
                          "Unknown property type '" + type->text + "'.");
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
                        add_error(DiagnosticCode::InvalidPropertyType, type->location,
                                  "Range minimum cannot exceed its maximum.");
                    }
                }
            }
        }
        expect(TokenKind::RightParenthesis, "Expected ')' after the property declaration.");
        expect(TokenKind::Equal, "Expected '=' before the property default value.");
        parse_default_value(property.default_value);
        if (match_identifier("Usage"))
        {
            const auto usage = expect_identifier("Expected Color, LinearData or Normal texture usage.");
            if (property.type != PropertyType::Texture2D || !usage)
            {
                add_error(DiagnosticCode::InvalidPropertyType, property.location,
                          "Texture Usage applies to Texture2D Properties only.");
            }
            else if (usage->text == "Color")
            {
                property.texture_usage = TextureUsage::Color;
            }
            else if (usage->text == "LinearData")
            {
                property.texture_usage = TextureUsage::LinearData;
            }
            else if (usage->text == "Normal")
            {
                property.texture_usage = TextureUsage::Normal;
            }
            else
            {
                add_error(DiagnosticCode::InvalidPropertyType, usage->location, "Unknown sampled texture usage.");
            }
        }
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
            add_error(DiagnosticCode::InvalidResourceGroup, group_token->location,
                      "Unknown Binding Group '" + group_token->text + "'.");
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
                add_error(DiagnosticCode::DuplicateResource, name->location,
                          "Duplicate resource '" + resource.name + "'.");
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

    bool ShaderParser::parse_parameters(ShaderAsset& asset)
    {
        if (!expect(TokenKind::LeftBrace, "Expected '{' after Parameters."))
        {
            return false;
        }
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            parse_parameter_group(asset);
        }
        return expect(TokenKind::RightBrace, "Expected '}' to close Parameters.").has_value();
    }

    bool ShaderParser::parse_parameter_group(ShaderAsset& asset)
    {
        const auto group_token = expect_identifier("Expected the Pass parameter Binding Group.");
        if (!group_token)
        {
            consume();
            return false;
        }
        const bool valid_group = group_token->text == "Pass";
        if (!valid_group)
        {
            add_error(DiagnosticCode::InvalidParameterGroup, group_token->location,
                      "Parameters only supports the Pass Binding Group.");
        }
        expect(TokenKind::LeftBrace, "Expected '{' after the parameter Binding Group.");
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            const auto name = expect_identifier("Expected a parameter name.");
            if (!name)
            {
                consume();
                continue;
            }
            Parameter parameter;
            parameter.name = name->text;
            parameter.location = name->location;
            validate_identifier(asset, *name, "parameter");
            const bool duplicate = contains_name(asset.parameters, parameter.name);
            if (duplicate)
            {
                add_error(DiagnosticCode::DuplicateParameter, name->location,
                          "Duplicate parameter '" + parameter.name + "'.");
            }
            expect(TokenKind::Colon, "Expected ':' after the parameter name.");
            const auto type = expect_identifier("Expected a parameter type.");
            if (type)
            {
                const auto parsed_type = parameter_type_from_name(type->text);
                if (!parsed_type)
                {
                    add_error(DiagnosticCode::InvalidParameterType, type->location,
                              "Parameters does not support type '" + type->text + "'.");
                }
                else
                {
                    parameter.type = *parsed_type;
                }
            }
            if (match(TokenKind::Equal))
            {
                parse_default_value(parameter.default_value);
                if (parameter.default_value.kind != DefaultValueKind::Numbers)
                {
                    add_error(DiagnosticCode::InvalidDefaultValue, parameter.default_value.location,
                              "Parameter defaults must be numeric.");
                }
            }
            if (!duplicate && valid_group)
            {
                asset.parameters.push_back(std::move(parameter));
            }
        }
        return expect(TokenKind::RightBrace, "Expected '}' to close the parameter Binding Group.").has_value();
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
            add_error(DiagnosticCode::InvalidResourceType, type->location,
                      "Unknown resource type '" + type->text + "'.");
            return false;
        }
        resource.kind = *kind;
        const bool has_element = match(TokenKind::LeftAngle);
        if (requires_element_type(*kind) != has_element)
        {
            add_error(DiagnosticCode::InvalidResourceType, type->location,
                      requires_element_type(*kind) ? "Resource type requires an element type."
                                                   : "Resource type does not accept an element type.");
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
                add_error(DiagnosticCode::InvalidResourceType, element->location,
                          "Resource element type '" + element->text + "' is not supported by this resource kind.");
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
        if (name->text == "Stages" || name->text == "Passes")
        {
            add_error(DiagnosticCode::ReservedIdentifier, name->location,
                      "Stages and Passes are reserved impact annotations.");
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
                    add_error(DiagnosticCode::InvalidVariant, value->location,
                              "Bool variant default must be true or false.");
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
                    if (std::find(variant.options.begin(), variant.options.end(), option->text) !=
                        variant.options.end())
                    {
                        add_error(DiagnosticCode::InvalidVariant, option->location,
                                  "Duplicate enum variant option '" + option->text + "'.");
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
                    add_error(DiagnosticCode::InvalidVariant, value->location,
                              "Enum variant default is not one of its options.");
                }
            }
        }
        else if (type)
        {
            add_error(DiagnosticCode::InvalidVariant, type->location, "Unknown variant type '" + type->text + "'.");
        }
        bool stages_declared = false;
        bool passes_declared = false;
        while (check_identifier("Stages") || check_identifier("Passes"))
        {
            const bool stages = match_identifier("Stages");
            if (!stages)
            {
                match_identifier("Passes");
            }
            auto& declared = stages ? stages_declared : passes_declared;
            if (declared)
            {
                add_error(DiagnosticCode::InvalidVariant, peek().location, "Duplicate variant impact declaration.");
            }
            declared = true;
            expect(TokenKind::LeftBrace, "Expected '{' before variant impact names.");
            std::uint32_t mask = 0u;
            while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
            {
                const auto value = expect_identifier("Expected a stage or mesh Pass role.");
                if (!value)
                {
                    consume();
                    break;
                }
                std::uint32_t bit = 0u;
                if (stages)
                {
                    if (value->text == "Vertex")
                    {
                        bit = static_cast<std::uint32_t>(ShaderStageFlags::Vertex);
                    }
                    else if (value->text == "Pixel")
                    {
                        bit = static_cast<std::uint32_t>(ShaderStageFlags::Pixel);
                    }
                    else if (value->text == "Compute")
                    {
                        bit = static_cast<std::uint32_t>(ShaderStageFlags::Compute);
                    }
                }
                else
                {
                    if (value->text == "Global")
                    {
                        bit = shader_pass_role_bit(ShaderPassRole::Global);
                    }
                    else if (value->text == "Forward")
                    {
                        bit = shader_pass_role_bit(ShaderPassRole::Forward);
                    }
                    else if (value->text == "ShadowDepth")
                    {
                        bit = shader_pass_role_bit(ShaderPassRole::ShadowDepth);
                    }
                    else if (value->text == "HitProxy")
                    {
                        bit = shader_pass_role_bit(ShaderPassRole::HitProxy);
                    }
                }
                if (bit == 0u || (mask & bit) != 0u)
                {
                    add_error(DiagnosticCode::InvalidVariant, value->location, "Unknown or duplicate impact name.");
                }
                mask |= bit;
                if (!match(TokenKind::Comma))
                {
                    break;
                }
            }
            expect(TokenKind::RightBrace, "Expected '}' after variant impact names.");
            if (mask == 0u)
            {
                add_error(DiagnosticCode::InvalidVariant, variant.location, "Variant impact cannot be empty.");
            }
            if (stages)
            {
                variant.affected_stages = static_cast<ShaderStageFlags>(mask);
            }
            else
            {
                variant.affected_passes = mask;
            }
        }
        if (!duplicate)
        {
            asset.variants.push_back(std::move(variant));
        }
        return true;
    }

    bool ShaderParser::parse_features(ShaderAsset& asset)
    {
        if (!expect(TokenKind::LeftBrace, "Expected '{' after Features."))
        {
            return false;
        }
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile))
        {
            const auto name = expect_identifier("Expected an engine feature.");
            if (!name)
            {
                consume();
                return false;
            }
            ShaderEngineFeatureDeclaration declaration;
            if (name->text == "Lighting")
            {
                declaration.feature = ShaderEngineFeature::Lighting;
            }
            else if (name->text == "Shadows")
            {
                declaration.feature = ShaderEngineFeature::Shadows;
            }
            else if (name->text == "Environment")
            {
                declaration.feature = ShaderEngineFeature::Environment;
            }
            else
            {
                add_error(DiagnosticCode::InvalidVariant, name->location,
                          "Unknown engine feature '" + name->text + "'.");
            }
            if (std::any_of(asset.features.begin(), asset.features.end(),
                            [&](const auto& existing)
                            {
                                return existing.feature == declaration.feature;
                            }))
            {
                add_error(DiagnosticCode::InvalidVariant, name->location, "Duplicate engine feature.");
            }
            if (match_identifier("When") && !parse_static_condition(declaration.condition))
            {
                return false;
            }
            asset.features.push_back(std::move(declaration));
        }
        return expect(TokenKind::RightBrace, "Expected '}' after Features.").has_value();
    }

    bool ShaderParser::parse_static_condition(ShaderStaticCondition& condition, std::size_t depth)
    {
        if (depth >= max_shader_static_condition_nodes || condition.nodes.size() >= max_shader_static_condition_nodes)
        {
            add_error(DiagnosticCode::InvalidVariant, peek().location, "Static condition exceeds the 64-node budget.");
            return false;
        }
        const auto operation = expect_identifier("Expected Equal, Profile, Capability, Not, All or Any.");
        if (!operation || !expect(TokenKind::LeftParenthesis, "Expected '(' after static condition operation."))
        {
            return false;
        }
        ShaderStaticConditionNode node;
        if (operation->text == "Equal")
        {
            const auto name = expect_identifier("Expected Material static option name.");
            expect(TokenKind::Comma, "Expected ',' after Material static option name.");
            const auto value = expect_identifier("Expected typed bool or enum value.");
            if (!name || !value)
            {
                return false;
            }
            node.comparison.name = name->text;
            if (value->text == "true" || value->text == "false")
            {
                node.comparison.boolean_value = value->text == "true";
            }
            else
            {
                node.comparison.kind = ShaderPermutationValueKind::Enumeration;
                node.comparison.enum_value = value->text;
            }
        }
        else if (operation->text == "Profile" || operation->text == "Capability")
        {
            const auto value = expect_identifier("Expected a profile or capability name.");
            if (!value)
            {
                return false;
            }
            if (operation->text == "Profile")
            {
                node.operation = ShaderStaticConditionOperation::Profile;
                if (value->text == "VulkanES31")
                {
                    node.profile = ShaderCompileProfile::VulkanES31;
                }
                else if (value->text == "D3D11FeatureLevel11_0")
                {
                    node.profile = ShaderCompileProfile::D3D11FeatureLevel11_0;
                }
                else if (value->text == "D3D12ShaderModel6")
                {
                    node.profile = ShaderCompileProfile::D3D12ShaderModel6;
                }
                else
                {
                    add_error(DiagnosticCode::InvalidVariant, value->location, "Unknown compile profile.");
                }
            }
            else
            {
                node.operation = ShaderStaticConditionOperation::Capability;
                if (value->text == "TextureCube")
                {
                    node.capability = ShaderStaticCapability::TextureCube;
                }
                else if (value->text == "ReadOnlyTypedBuffer")
                {
                    node.capability = ShaderStaticCapability::ReadOnlyTypedBuffer;
                }
                else if (value->text == "Rgba16FloatSampled")
                {
                    node.capability = ShaderStaticCapability::Rgba16FloatSampled;
                }
                else
                {
                    add_error(DiagnosticCode::InvalidVariant, value->location, "Unknown static capability.");
                }
            }
        }
        else if (operation->text == "Not" || operation->text == "All" || operation->text == "Any")
        {
            node.operation = operation->text == "Not"
                                 ? ShaderStaticConditionOperation::Not
                                 : (operation->text == "All" ? ShaderStaticConditionOperation::All
                                                             : ShaderStaticConditionOperation::Any);
            do
            {
                if (!parse_static_condition(condition, depth + 1u))
                {
                    return false;
                }
                ++node.argument_count;
            } while (match(TokenKind::Comma));
            if (node.operation == ShaderStaticConditionOperation::Not && node.argument_count != 1u)
            {
                add_error(DiagnosticCode::InvalidVariant, operation->location, "Not requires one argument.");
            }
        }
        else
        {
            add_error(DiagnosticCode::InvalidVariant, operation->location, "Unknown static condition operation.");
            return false;
        }
        if (!expect(TokenKind::RightParenthesis, "Expected ')' after static condition.") ||
            condition.nodes.size() >= max_shader_static_condition_nodes)
        {
            add_error(DiagnosticCode::InvalidVariant, operation->location, "Invalid or oversized static condition.");
            return false;
        }
        condition.nodes.push_back(std::move(node));
        return true;
    }

    bool ShaderParser::parse_vertex_factories(ShaderAsset& asset)
    {
        if (!expect(TokenKind::LeftBrace, "Expected '{' after VertexFactories."))
        {
            return false;
        }
        do
        {
            const auto factory = expect_identifier("Expected Local or GPUSkin.");
            if (!factory)
            {
                break;
            }
            std::uint32_t flag = 0u;
            if (factory->text == "Local")
            {
                flag = local_vertex_factory_support;
            }
            else if (factory->text == "GPUSkin")
            {
                flag = gpu_skin_vertex_factory_support;
            }
            if (flag == 0u || (asset.vertex_factory_support & flag) != 0u)
            {
                add_error(DiagnosticCode::InvalidShaderName, factory->location, "Unknown or duplicate VertexFactory.");
            }
            asset.vertex_factory_support |= flag;
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightBrace, "Expected '}' after VertexFactories.");
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
        bool has_role = false;
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
            if (match_identifier("CoverageFunction"))
            {
                if (const auto function = expect_identifier("Expected a shared coverage function."))
                {
                    if (!pass.coverage_function.empty() || asset.geometry != ShaderGeometryMode::Standard)
                    {
                        add_error(DiagnosticCode::InvalidPassState, function->location,
                                  "CoverageFunction is declared once and only for Standard geometry.");
                    }
                    pass.coverage_function = function->text;
                }
                continue;
            }
            if (match_identifier("Role"))
            {
                if (has_role)
                {
                    add_error(DiagnosticCode::DuplicatePassState, peek().location,
                              "Pass must declare Role exactly once.");
                }
                has_role = true;
                if (const auto role = expect_identifier("Expected a Pass role."))
                {
                    if (role->text == "Forward")
                    {
                        pass.role = ShaderPassRole::Forward;
                    }
                    else if (role->text == "ShadowDepth")
                    {
                        pass.role = ShaderPassRole::ShadowDepth;
                    }
                    else if (role->text == "HitProxy")
                    {
                        pass.role = ShaderPassRole::HitProxy;
                    }
                    else if (role->text != "Global")
                    {
                        add_error(DiagnosticCode::InvalidPassName, role->location, "Unknown Pass role.");
                    }
                }
                continue;
            }
            if (check_identifier("HLSLVS") || check_identifier("HLSLPS") || check_identifier("HLSLCS"))
            {
                const Token block_name = consume();
                const ShaderStage stage =
                    block_name.text == "HLSLVS"
                        ? ShaderStage::Vertex
                        : (block_name.text == "HLSLPS" ? ShaderStage::Pixel : ShaderStage::Compute);
                HlslBlock block;
                parse_hlsl_block(block);
                if (block.entry_points.empty())
                {
                    add_error(DiagnosticCode::MissingEntryPoint, block_name.location,
                              "Stage block is missing its entry pragma.");
                }
                else if (block.entry_points.size() != 1u || block.entry_points.front().stage != stage)
                {
                    add_error(DiagnosticCode::MixedProgramStages, block_name.location,
                              "Each stage block requires exactly one matching stage pragma.");
                }
                const bool duplicate = std::any_of(pass.programs.begin(), pass.programs.end(),
                                                   [&](const HlslBlock& existing)
                                                   {
                                                       return !existing.entry_points.empty() &&
                                                              existing.entry_points.front().stage == stage;
                                                   });
                if (duplicate)
                {
                    add_error(DiagnosticCode::DuplicateEntryPoint, block_name.location,
                              "Duplicate stage program block.");
                }
                pass.programs.push_back(std::move(block));
                continue;
            }

            if (peek().kind == TokenKind::Identifier && is_pass_state_name(peek().text))
            {
                const Token state_name = consume();
                pass.has_explicit_graphics_state = true;
                if (!declared_states.insert(state_name.text).second)
                {
                    add_error(DiagnosticCode::DuplicatePassState, state_name.location,
                              "Pass state '" + state_name.text + "' is declared more than once.");
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
                    const bool valid = is_valid_pass_state_value(state_name.text, value->text);
                    if (!valid)
                    {
                        add_error(DiagnosticCode::InvalidPassState, value->location,
                                  "Invalid value '" + value->text + "' for Pass state '" + state_name.text + "'.");
                    }
                    if (!valid)
                    {
                        continue;
                    }
                    if (state_name.text == "PrimitiveTopology")
                    {
                        pass.state.primitive_topology = primitive_topology_from_name(value->text);
                    }
                    else if (state_name.text == "Cull")
                    {
                        pass.state.cull_mode = cull_mode_from_name(value->text);
                    }
                    else if (state_name.text == "FrontFace")
                    {
                        pass.state.front_face = value->text == "Clockwise"
                                                    ? ShaderGraphicsPassState::FrontFace::Clockwise
                                                    : ShaderGraphicsPassState::FrontFace::CounterClockwise;
                    }
                    else if (state_name.text == "Fill")
                    {
                        pass.state.fill_mode = value->text == "Wireframe" ? ShaderGraphicsPassState::FillMode::Wireframe
                                                                          : ShaderGraphicsPassState::FillMode::Solid;
                    }
                    else if (state_name.text == "DepthTest")
                    {
                        pass.state.depth_test_enable = value->text != "Off";
                        if (pass.state.depth_test_enable)
                        {
                            pass.state.depth_compare_operation = compare_operation_from_name(value->text);
                        }
                    }
                    else if (state_name.text == "DepthWrite")
                    {
                        pass.state.depth_write_enable = value->text == "On";
                    }
                    else if (state_name.text == "ColorWrite")
                    {
                        pass.state.color_write_mask = color_write_mask_from_name(value->text);
                    }
                }
                continue;
            }

            const Token unexpected = consume();
            add_error(DiagnosticCode::InvalidPassState, unexpected.location,
                      "Unknown Pass field '" + unexpected.text + "'.");
        }
        expect(TokenKind::RightBrace, "Expected '}' to close the Pass.");
        if (!has_role)
        {
            add_error(DiagnosticCode::InvalidPassName, pass.location, "Pass requires an explicit Role.");
        }
        if (pass.programs.empty())
        {
            add_error(DiagnosticCode::MissingEntryPoint, pass.location, "Pass is missing a stage program block.");
        }
        else
        {
            validate_program(pass, asset.geometry);
        }
        asset.passes.push_back(std::move(pass));
        return true;
    }

    bool ShaderParser::parse_stencil_state(ShaderPass& pass)
    {
        if (match_identifier("Off"))
        {
            pass.state.stencil = ShaderGraphicsPassState::StencilState{};
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
                add_error(DiagnosticCode::InvalidPassState, field->location,
                          "Stencil field '" + field->text + "' is declared more than once.");
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
                        add_error(DiagnosticCode::InvalidPassState, value->location,
                                  "Stencil mask must be an integer from 0 through 255.");
                    }
                    else if (field->text == "ReadMask")
                    {
                        pass.state.stencil.read_mask = static_cast<std::uint8_t>(parsed);
                    }
                    else
                    {
                        pass.state.stencil.write_mask = static_cast<std::uint8_t>(parsed);
                    }
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
            add_error(DiagnosticCode::InvalidPassState, field->location,
                      "Unknown Stencil field '" + field->text + "'.");
        }
        expect(TokenKind::RightBrace, "Expected '}' to close Stencil state.");
        if (has_front_and_back == (has_front || has_back) || (!has_front_and_back && !(has_front && has_back)))
        {
            add_error(DiagnosticCode::InvalidPassState, pass.location,
                      "Stencil requires FrontAndBack or both Front and Back.");
            return false;
        }
        pass.state.stencil.mode = has_front_and_back ? ShaderGraphicsPassState::StencilMode::FrontAndBack
                                                     : ShaderGraphicsPassState::StencilMode::SeparateFaces;
        return true;
    }

    bool ShaderParser::parse_stencil_face(ShaderGraphicsPassState::StencilFaceState& face)
    {
        const char* names[] = {"Compare", "Fail", "DepthFail", "Pass"};
        for (std::size_t index = 0; index < 4; ++index)
        {
            if (!match_identifier(names[index]))
            {
                add_error(DiagnosticCode::InvalidPassState, peek().location,
                          "Expected Stencil operation field '" + std::string(names[index]) + "'.");
                return false;
            }
            const auto value = expect_identifier("Expected a Stencil operation value.");
            if (!value)
            {
                return false;
            }
            const bool valid = index == 0 ? is_stencil_compare(value->text) : is_stencil_operation(value->text);
            if (!valid)
            {
                add_error(DiagnosticCode::InvalidPassState, value->location,
                          "Invalid Stencil operation value '" + value->text + "'.");
            }
            if (!valid)
            {
                continue;
            }
            if (index == 0)
            {
                face.compare_operation = compare_operation_from_name(value->text);
            }
            else if (index == 1)
            {
                face.fail_operation = stencil_operation_from_name(value->text);
            }
            else if (index == 2)
            {
                face.depth_fail_operation = stencil_operation_from_name(value->text);
            }
            else
            {
                face.pass_operation = stencil_operation_from_name(value->text);
            }
        }
        return true;
    }

    bool ShaderParser::parse_blend_state(ShaderPass& pass)
    {
        if (match_identifier("Off"))
        {
            pass.state.blend = ShaderGraphicsPassState::BlendState{};
            return true;
        }
        if (!expect(TokenKind::LeftBrace, "Expected 'Off' or '{' after Blend."))
        {
            return false;
        }
        pass.state.blend.enabled = true;
        struct BlendLine
        {
            const char* name;
            ShaderGraphicsPassState::BlendFactor* source;
            ShaderGraphicsPassState::BlendFactor* destination;
            ShaderGraphicsPassState::BlendOperation* operation;
        };
        BlendLine lines[] = {{"Color", &pass.state.blend.source_color_factor,
                              &pass.state.blend.destination_color_factor, &pass.state.blend.color_operation},
                             {"Alpha", &pass.state.blend.source_alpha_factor,
                              &pass.state.blend.destination_alpha_factor, &pass.state.blend.alpha_operation}};
        for (BlendLine& line : lines)
        {
            if (!match_identifier(line.name))
            {
                add_error(DiagnosticCode::InvalidPassState, peek().location,
                          "Expected Blend field '" + std::string(line.name) + "'.");
                break;
            }
            const auto source = expect_identifier("Expected source Blend factor.");
            const auto destination = expect_identifier("Expected destination Blend factor.");
            const auto operation = expect_identifier("Expected Blend operation.");
            if (source)
            {
                if (!is_blend_factor(source->text))
                {
                    add_error(DiagnosticCode::InvalidPassState, source->location,
                              "Invalid Blend factor '" + source->text + "'.");
                }
                else
                {
                    *line.source = blend_factor_from_name(source->text);
                }
            }
            if (destination)
            {
                if (!is_blend_factor(destination->text))
                {
                    add_error(DiagnosticCode::InvalidPassState, destination->location,
                              "Invalid Blend factor '" + destination->text + "'.");
                }
                else
                {
                    *line.destination = blend_factor_from_name(destination->text);
                }
            }
            if (operation)
            {
                if (!is_blend_operation(operation->text))
                {
                    add_error(DiagnosticCode::InvalidPassState, operation->location,
                              "Invalid Blend operation '" + operation->text + "'.");
                }
                else
                {
                    *line.operation = blend_operation_from_name(operation->text);
                }
            }
        }
        return expect(TokenKind::RightBrace, "Expected '}' to close Blend state.").has_value();
    }

    bool ShaderParser::parse_hlsl_block(HlslBlock& block)
    {
        if (lookahead)
        {
            add_error(DiagnosticCode::UnexpectedToken, lookahead->location,
                      "Internal parser state prevented raw HLSL capture.");
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
        add_error(DiagnosticCode::InvalidDefaultValue, peek().location,
                  "Expected a property or resource default value.");
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
                line_text.size() > first + 7 && (line_text[first + 7] == ' ' || line_text[first + 7] == '\t'))
            {
                std::istringstream pragma(line_text.substr(first + 7));
                std::string stage_name;
                std::string entry_name;
                std::string trailing;
                pragma >> stage_name >> entry_name >> trailing;
                const SourceLocation location{block.location.path, block.location.offset, line_number, first + 1};
                const auto stage = stage_from_pragma(stage_name);
                if (!stage || entry_name.empty() || !trailing.empty())
                {
                    add_error(DiagnosticCode::UnknownPragma, location,
                              "Expected '#pragma vertex|pixel|compute entry_name'.");
                }
                else
                {
                    const bool duplicate = std::any_of(block.entry_points.begin(), block.entry_points.end(),
                                                       [&](const EntryPoint& entry)
                                                       {
                                                           return entry.stage == *stage;
                                                       });
                    if (duplicate)
                    {
                        add_error(DiagnosticCode::DuplicateEntryPoint, location,
                                  "Shader stage pragma is declared more than once.");
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

    void ShaderParser::validate_program(ShaderPass& pass, ShaderGeometryMode geometry)
    {
        const bool has_vertex = std::any_of(pass.programs.begin(), pass.programs.end(),
                                            [](const HlslBlock& block)
                                            {
                                                return !block.entry_points.empty() &&
                                                       block.entry_points.front().stage == ShaderStage::Vertex;
                                            });
        const bool has_pixel = std::any_of(pass.programs.begin(), pass.programs.end(),
                                           [](const HlslBlock& block)
                                           {
                                               return !block.entry_points.empty() &&
                                                      block.entry_points.front().stage == ShaderStage::Pixel;
                                           });
        const bool has_compute = std::any_of(pass.programs.begin(), pass.programs.end(),
                                             [](const HlslBlock& block)
                                             {
                                                 return !block.entry_points.empty() &&
                                                        block.entry_points.front().stage == ShaderStage::Compute;
                                             });
        if (geometry == ShaderGeometryMode::Standard)
        {
            if (pass.role != ShaderPassRole::Forward || has_vertex || has_compute || !has_pixel ||
                pass.state.blend.enabled)
            {
                add_error(DiagnosticCode::MixedProgramStages, pass.location,
                          "Standard geometry declares a Forward HLSLPS shading function; VS and other roles are "
                          "generated, and blending is unsupported.");
            }
            return;
        }
        if (pass.role != ShaderPassRole::Global &&
            (has_compute || (!has_pixel && pass.role != ShaderPassRole::ShadowDepth)))
        {
            add_error(DiagnosticCode::MixedProgramStages, pass.location,
                      "Mesh roles require graphics stages; Forward and HitProxy require HLSLVS and HLSLPS.");
        }
        if (has_compute && (has_vertex || has_pixel))
        {
            add_error(DiagnosticCode::MixedProgramStages, pass.location,
                      "A Pass cannot mix compute and graphics entry points.");
        }
        else if (has_compute)
        {
            if (pass.has_explicit_graphics_state)
            {
                add_error(DiagnosticCode::InvalidPassState, pass.location,
                          "Compute Pass cannot declare graphics pipeline state.");
            }
            return;
        }
        else if (!has_vertex)
        {
            add_error(DiagnosticCode::MissingEntryPoint, pass.location,
                      "Graphics Pass requires a vertex pragma; pixel is optional.");
        }
    }

    void ShaderParser::validate_identifier(ShaderAsset& asset, const Token& token, std::string_view category)
    {
        if (asset.usage == ShaderUsage::Material)
        {
            for (const auto& parameter : builtin_forward_parameters)
            {
                if (token.text == parameter.name)
                {
                    add_error(DiagnosticCode::ReservedIdentifier, token.location,
                              "Material identifier collides with an engine Forward parameter.");
                }
            }
            for (const auto& resource : builtin_forward_resources)
            {
                if (token.text == resource.name)
                {
                    add_error(DiagnosticCode::ReservedIdentifier, token.location,
                              "Material identifier collides with an engine Forward resource.");
                }
            }
        }
        if (token.text.rfind("toy3d_", 0) == 0 || token.text.rfind("TOY3D_", 0) == 0)
        {
            add_error(DiagnosticCode::ReservedIdentifier, token.location,
                      "Identifier '" + token.text + "' uses a reserved Toy3d prefix.");
        }
        const bool conflict = (category != "property" && contains_name(asset.properties, token.text)) ||
                              (category != "parameter" && contains_name(asset.parameters, token.text)) ||
                              (category != "resource" && contains_name(asset.resources, token.text)) ||
                              (category != "variant" && contains_name(asset.variants, token.text));
        if (conflict)
        {
            add_error(DiagnosticCode::IdentifierConflict, token.location,
                      "The " + std::string(category) + " identifier '" + token.text +
                          "' conflicts with another generated HLSL identifier.");
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
} // namespace toy3d::shader
