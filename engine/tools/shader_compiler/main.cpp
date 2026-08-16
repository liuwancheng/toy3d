#include "frontend/shader_parser.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    const char* severity_name(toy3d::shader::DiagnosticSeverity severity)
    {
        return severity == toy3d::shader::DiagnosticSeverity::Error ? "error" : "warning";
    }
}

int main(int argument_count, char** arguments)
{
    if (argument_count != 3 || std::string(arguments[1]) != "parse")
    {
        std::cerr << "Usage: Toy3dShaderCompiler parse <input.shader>\n";
        return 2;
    }

    const std::string path = arguments[2];
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        std::cerr << path << ": error: unable to open Shader asset.\n";
        return 2;
    }
    const std::string source{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const toy3d::shader::ParseResult result = toy3d::shader::parse_shader(source, path);
    for (const toy3d::shader::Diagnostic& diagnostic : result.diagnostics)
    {
        std::cerr << diagnostic.location.path << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << ": " << severity_name(diagnostic.severity)
                  << ": " << diagnostic.message << '\n';
    }
    if (!result.succeeded())
    {
        return 1;
    }

    const toy3d::shader::ShaderAsset& asset = *result.asset;
    std::cout << "Parsed Shader '" << asset.name << "' (v" << asset.version << ") with "
              << asset.properties.size() << " properties, " << asset.resources.size()
              << " resources, " << asset.variants.size() << " variants, and "
              << asset.passes.size() << " passes.\n";
    return 0;
}
