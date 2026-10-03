#include "frontend/shader_parser.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    int failure_count = 0;

    std::string read_test_data(const std::string& name)
    {
        const std::string path = std::string(TOY3D_SHADER_TEST_DATA_DIR) + '/' + name;
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            std::cerr << "FAILED: unable to open " << path << '\n';
            ++failure_count;
            return {};
        }
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    bool contains_diagnostic(const toy3d::shader::ParseResult& result, toy3d::shader::DiagnosticCode code)
    {
        for (const toy3d::shader::Diagnostic& diagnostic : result.diagnostics)
        {
            if (diagnostic.code == code)
            {
                check(diagnostic.location.line > 0, "diagnostic must include a source line");
                check(diagnostic.location.column > 0, "diagnostic must include a source column");
                return true;
            }
        }
        return false;
    }

    toy3d::shader::ParseResult parse_test_data(const std::string& name)
    {
        const std::string source = read_test_data(name);
        return toy3d::shader::parse_shader(source, name);
    }

    void test_valid_shader()
    {
        const toy3d::shader::ParseResult result = parse_test_data("frontend_valid.shader");
        check(result.succeeded(), "valid Shader asset must parse");
        if (!result.asset)
        {
            return;
        }
        const toy3d::shader::ShaderAsset& asset = *result.asset;
        check(asset.name == "Tests/FrontendValid", "Shader name must be preserved");
        check(asset.version == 2, "Shader version must be parsed");
        check(asset.properties.size() == 3, "all Properties must be parsed");
        check(asset.parameters.size() == 2, "all Pass Parameters must be parsed");
        check(asset.resources.size() == 4, "all Resources must be parsed");
        check(asset.variants.size() == 2, "all Variants must be parsed");
        check(asset.includes.size() == 1, "HLSLINCLUDE must be captured");
        check(asset.passes.size() == 1, "Pass must be parsed");
        if (!asset.passes.empty())
        {
            check(asset.passes[0].programs.size() == 2, "independent graphics stage blocks must be extracted");
            const toy3d::shader::ShaderPass& pass = asset.passes[0];
            check(pass.state.stencil.mode == toy3d::shader::ShaderGraphicsPassState::StencilMode::FrontAndBack,
                  "structured Stencil state must be parsed");
            check(pass.state.stencil.read_mask == 127, "Stencil read mask must be parsed");
            check(pass.state.stencil.front.depth_fail_operation ==
                      toy3d::shader::ShaderGraphicsPassState::StencilOperation::Replace,
                  "Stencil face operations must be parsed");
            check(pass.state.blend.enabled, "structured Blend state must be enabled");
            check(pass.state.blend.source_color_factor ==
                      toy3d::shader::ShaderGraphicsPassState::BlendFactor::SourceAlpha,
                  "Blend factors must be parsed");
            check(pass.state.color_write_mask == toy3d::shader::ShaderGraphicsPassState::ColorWriteMask::RedGreenBlue,
                  "single-target ColorWrite mask must be parsed");
            check(asset.resources[0].kind == toy3d::shader::ResourceKind::Texture2D,
                  "resource kind must be strongly typed");
            check(asset.resources[0].element_type == toy3d::shader::ResourceElementType::Float4,
                  "resource element type must be strongly typed");
            check(asset.resources[2].kind == toy3d::shader::ResourceKind::ComparisonSampler,
                  "ComparisonSampler must remain distinct from a regular Sampler");
        }
    }

    void test_error(const std::string& name, toy3d::shader::DiagnosticCode expected)
    {
        const toy3d::shader::ParseResult result = parse_test_data(name);
        check(!result.succeeded(), name + " must fail parsing");
        check(!result.asset.has_value(), name + " must not return a partial asset");
        check(contains_diagnostic(result, expected), name + " must return the expected diagnostic");
    }

    void test_vertex_only_graphics_pass()
    {
        const toy3d::shader::ParseResult result = parse_test_data("vertex_only.shader");
        check(result.succeeded(), "graphics Pass may omit the pixel entry point");
        if (result.asset && !result.asset->passes.empty())
        {
            const toy3d::shader::ShaderGraphicsPassState& state = result.asset->passes[0].state;
            check(state.depth_test_enable && state.depth_compare_operation ==
                                                 toy3d::shader::ShaderGraphicsPassState::CompareOperation::GreaterEqual,
                  "omitted Pass state must normalize to reversed-Z defaults");
            check(state.depth_write_enable, "omitted DepthWrite must normalize to On");
        }
    }

    void test_diagnostic_formatting()
    {
        const toy3d::shader::Diagnostic diagnostic{toy3d::shader::DiagnosticSeverity::Error,
                                                   toy3d::shader::DiagnosticCode::IncludeCycle,
                                                   {"/Engine/Test.shader", 0, 12, 7},
                                                   "include cycle detected"};
        check(toy3d::shader::format_diagnostic(diagnostic) ==
                  "/Engine/Test.shader:12:7: error [IncludeCycle]: include cycle detected",
              "diagnostics must use the shared stable display format");
    }

    void test_v2_contract()
    {
        using namespace toy3d::shader;
        const std::string source = R"(
Shader "Tests/V2Contract"
{
    Version 2
    Usage Material
    Geometry Custom
    VertexFactories { Local, GPUSkin }
    Pass "DisplayName"
    {
        Role Forward
        DepthTest Always
        DepthWrite Off
        HLSLVS
        #pragma vertex vs_main
        float4 vs_main() : SV_Position { return 0; }
        ENDHLSL
        HLSLPS
        #pragma pixel ps_main
        float4 ps_main() : SV_Target0 { return 1; }
        ENDHLSL
    }
})";
        const auto parsed = parse_shader(source, "v2_contract.shader");
        check(parsed.succeeded(), "Explicit v2 Custom mesh source must parse");
        if (parsed.asset)
        {
            check(parsed.asset->usage == ShaderUsage::Material &&
                      parsed.asset->geometry == ShaderGeometryMode::Custom &&
                      parsed.asset->vertex_factory_support == all_vertex_factory_support &&
                      parsed.asset->passes.front().name == "DisplayName" &&
                      parsed.asset->passes.front().role == ShaderPassRole::Forward,
                  "Usage, role and factory support must come from declarations, not names/includes");
            const auto& pass = parsed.asset->passes.front();
            check(pass.programs[0].source.find("ps_main") == std::string::npos &&
                      pass.programs[1].source.find("vs_main") == std::string::npos,
                  "Distinct stage sources must remain independent in the AST");
            check(!pass.state.depth_write_enable &&
                      pass.state.depth_compare_operation == ShaderGraphicsPassState::CompareOperation::Always,
                  "Custom source retains its author-defined depth strategy");
        }
        const auto replaced = [&](const std::string& from, const std::string& to)
        {
            std::string changed = source;
            const auto at = changed.find(from);
            check(at != std::string::npos, "Test mutation must target an existing declaration");
            changed.replace(at, from.size(), to);
            return parse_shader(changed, "v2_invalid.shader");
        };
        check(contains_diagnostic(replaced("Version 2", "Version 1"), DiagnosticCode::InvalidVersion),
              "Version 1 must be rejected rather than interpreted or migrated");
        check(!replaced("Usage Material", "").succeeded(), "Missing Usage must fail");
        check(!replaced("Role Forward", "").succeeded(), "Missing Role must fail");
        check(!replaced("Role Forward", "Role Unknown").succeeded(), "Unknown role must fail");
        check(!replaced("Local, GPUSkin", "Local, Local").succeeded(), "Duplicate factories must fail");
        check(!replaced("Local, GPUSkin", "Unknown").succeeded(), "Unknown factories must fail");
        check(replaced("Local, GPUSkin", "GPUSkin").succeeded(),
              "Offline authoring supports a declared factory subset independent of Editor publication policy");
        check(!replaced("Geometry Custom", "Geometry Standard").succeeded(),
              "Standard source cannot provide its own vertex entry");
        check(!replaced("HLSLPS", "HLSLVS").succeeded(), "Stage block/pragma mismatch must fail");
        check(!replaced("HLSLPS\n        #pragma pixel ps_main\n        float4 ps_main() : SV_Target0 { return 1; }\n  "
                        "      ENDHLSL",
                        "")
                   .succeeded(),
              "Forward without a pixel stage must fail");
        check(!replaced("Role Forward", "Role Forward\n Role Forward").succeeded(),
              "Duplicate role declaration must fail");
        check(!replaced("DepthWrite Off", "DepthWrite Off\n Blend On").succeeded(),
              "Mesh transparent blending must fail");
        const auto featured = replaced("Pass \"DisplayName\"", R"(
    Variants { USE_LIGHTING : bool = true }
    Features {
        Lighting When Equal(USE_LIGHTING, true)
        Shadows When All(Profile(VulkanES31), Not(Capability(TextureCube)))
        Environment When Any(Profile(D3D12ShaderModel6), Capability(Rgba16FloatSampled))
    }
    SupportedWhen Profile(VulkanES31)
    Pass "DisplayName")");
        const auto geometry = replaced("Pass \"DisplayName\"", R"(
    Variants { NORMAL : bool = false }
    GeometryRequirements { TangentFrame When Equal(NORMAL, true) }
    Pass "DisplayName")");
        check(geometry.succeeded() && geometry.asset->declares_tangent_frame &&
                  geometry.asset->tangent_frame_when.nodes.size() == 1u,
              "Custom geometry requirements preserve typed static conditions");
        check(!replaced("Pass \"DisplayName\"", "SurfaceInputs { Tangent } Pass \"DisplayName\"").succeeded(),
              "Custom sources cannot request Standard interpolation generation");
        check(!replaced("Pass \"DisplayName\"", "GeometryRequirements { Unknown } Pass \"DisplayName\"").succeeded(),
              "Unknown geometry capability rejects at the declaration");
        check(
            !replaced("Pass \"DisplayName\"", "GeometryRequirements { TangentFrame TangentFrame } Pass \"DisplayName\"")
                 .succeeded(),
            "Duplicate geometry capabilities reject");
        const auto impacts = replaced(
            "Pass \"DisplayName\"",
            "Variants { SHADE : bool = true Stages { Pixel } Passes { Forward, HitProxy } } Pass \"DisplayName\"");
        check(impacts.succeeded() && impacts.asset->variants.front().affected_stages == ShaderStageFlags::Pixel &&
                  impacts.asset->variants.front().affected_passes ==
                      (shader_pass_role_bit(ShaderPassRole::Forward) | shader_pass_role_bit(ShaderPassRole::HitProxy)),
              "Variants preserve explicit stage and Pass impacts");
        for (const std::string invalid : {"Stages { }", "Stages { Pixel, Pixel }", "Stages { Unknown }", "Passes { }",
                                          "Passes { Forward } Passes { HitProxy }"})
        {
            check(!replaced("Pass \"DisplayName\"",
                            "Variants { SHADE : bool = true " + invalid + " } Pass \"DisplayName\"")
                       .succeeded(),
                  "Malformed impact annotation rejects: " + invalid);
        }
        check(featured.succeeded() && featured.asset->features.size() == 3u &&
                  featured.asset->features[1].condition.nodes.size() == 4u,
              "Engine features parse bounded typed postfix expressions");
        check(
            !replaced("Pass \"DisplayName\"", "Features { Lighting Shadows Shadows } Pass \"DisplayName\"").succeeded(),
            "Duplicate engine features reject");
        check(!replaced("Pass \"DisplayName\"", "Features { Unknown } Pass \"DisplayName\"").succeeded(),
              "Unknown engine feature names reject");
        check(!replaced("Pass \"DisplayName\"",
                        "SupportedWhen Not(Profile(VulkanES31), Profile(VulkanES31)) Pass \"DisplayName\"")
                   .succeeded(),
              "Static condition arity rejects at its declaration");
        check(!replaced("Pass \"DisplayName\"", "SupportedWhen Capability(Unknown) Pass \"DisplayName\"").succeeded(),
              "Unknown capability names reject");
        check(!replaced("Pass \"DisplayName\"", "Parameters { Pass { custom_light : Float } } Pass \"DisplayName\"")
                   .succeeded(),
              "Material sources cannot author a different engine Pass ABI");
        check(!replaced("Pass \"DisplayName\"",
                        "Resources { Pass { custom_cube : TextureCube<Float4> } } Pass \"DisplayName\"")
                   .succeeded(),
              "Material sources cannot add arbitrary Pass resources");
        check(!replaced("Pass \"DisplayName\"",
                        "Properties { scene_light_color (\"Collision\", Float4) = (0,0,0,0) } Pass \"DisplayName\"")
                   .succeeded(),
              "Material properties cannot shadow engine Forward names");
    }
} // namespace

int main()
{
    test_valid_shader();
    test_vertex_only_graphics_pass();
    test_diagnostic_formatting();
    test_v2_contract();
    test_error("duplicate_property.shader", toy3d::shader::DiagnosticCode::DuplicateProperty);
    test_error("missing_entry.shader", toy3d::shader::DiagnosticCode::MissingEntryPoint);
    test_error("invalid_state.shader", toy3d::shader::DiagnosticCode::InvalidPassState);
    test_error("unsupported_constant_alpha.shader", toy3d::shader::DiagnosticCode::InvalidPassState);
    test_error("unknown_field.shader", toy3d::shader::DiagnosticCode::UnexpectedToken);
    test_error("unterminated_hlsl.shader", toy3d::shader::DiagnosticCode::UnterminatedHlslBlock);
    test_error("invalid_resource_type.shader", toy3d::shader::DiagnosticCode::InvalidResourceType);
    test_error("identifier_conflict.shader", toy3d::shader::DiagnosticCode::IdentifierConflict);
    test_error("duplicate_pass_state.shader", toy3d::shader::DiagnosticCode::DuplicatePassState);
    test_error("compute_graphics_state.shader", toy3d::shader::DiagnosticCode::InvalidPassState);
    test_error("invalid_shader_name.shader", toy3d::shader::DiagnosticCode::InvalidShaderName);
    test_error("reserved_identifier.shader", toy3d::shader::DiagnosticCode::ReservedIdentifier);
    test_error("duplicate_parameters.shader", toy3d::shader::DiagnosticCode::DuplicateSection);
    test_error("invalid_parameter_group.shader", toy3d::shader::DiagnosticCode::InvalidParameterGroup);
    test_error("invalid_parameter_type.shader", toy3d::shader::DiagnosticCode::InvalidParameterType);
    test_error("parameter_identifier_conflict.shader", toy3d::shader::DiagnosticCode::IdentifierConflict);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " frontend test assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Shader frontend tests passed.\n";
    return 0;
}
