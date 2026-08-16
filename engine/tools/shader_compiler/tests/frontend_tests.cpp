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
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    bool contains_diagnostic(
        const toy3d::shader::ParseResult& result,
        toy3d::shader::DiagnosticCode code)
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
        check(asset.resources.size() == 3, "all Resources must be parsed");
        check(asset.variants.size() == 2, "all Variants must be parsed");
        check(asset.includes.size() == 1, "HLSLINCLUDE must be captured");
        check(asset.passes.size() == 1, "Pass must be parsed");
        if (!asset.passes.empty())
        {
            check(asset.passes[0].program.entry_points.size() == 2, "graphics entry points must be extracted");
            check(asset.passes[0].states.size() == 9, "portable Pass states must be parsed");
        }
    }

    void test_error(const std::string& name, toy3d::shader::DiagnosticCode expected)
    {
        const toy3d::shader::ParseResult result = parse_test_data(name);
        check(!result.succeeded(), name + " must fail parsing");
        check(!result.asset.has_value(), name + " must not return a partial asset");
        check(contains_diagnostic(result, expected), name + " must return the expected diagnostic");
    }
}

int main()
{
    test_valid_shader();
    test_error("duplicate_property.shader", toy3d::shader::DiagnosticCode::DuplicateProperty);
    test_error("missing_entry.shader", toy3d::shader::DiagnosticCode::MissingEntryPoint);
    test_error("invalid_state.shader", toy3d::shader::DiagnosticCode::InvalidPassState);
    test_error("unknown_field.shader", toy3d::shader::DiagnosticCode::UnexpectedToken);
    test_error("unterminated_hlsl.shader", toy3d::shader::DiagnosticCode::UnterminatedHlslBlock);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " frontend test assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Shader frontend tests passed.\n";
    return 0;
}
