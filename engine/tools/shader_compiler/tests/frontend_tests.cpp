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
        check(asset.version == 1, "Shader version must be parsed");
        check(asset.properties.size() == 3, "all Properties must be parsed");
        check(asset.parameters.size() == 2, "all Pass Parameters must be parsed");
        check(asset.resources.size() == 4, "all Resources must be parsed");
        check(asset.variants.size() == 2, "all Variants must be parsed");
        check(asset.includes.size() == 1, "HLSLINCLUDE must be captured");
        check(asset.passes.size() == 1, "Pass must be parsed");
        if (!asset.passes.empty())
        {
            check(asset.passes[0].program.entry_points.size() == 2, "graphics entry points must be extracted");
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
} // namespace

int main()
{
    test_valid_shader();
    test_vertex_only_graphics_pass();
    test_diagnostic_formatting();
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
