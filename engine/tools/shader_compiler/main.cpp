#include "compiler/toolchain_manifest.h"
#include "frontend/shader_parser.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace
{
    const char* severity_name(toy3d::shader::DiagnosticSeverity severity)
    {
        return severity == toy3d::shader::DiagnosticSeverity::Error ? "error" : "warning";
    }

    void print_usage()
    {
        std::cerr
            << "Usage:\n"
            << "  Toy3dShaderCompiler [--toolchain-root <path>] parse <input.shader>\n"
            << "  Toy3dShaderCompiler [--toolchain-root <path>] toolchain-info\n";
    }

    std::filesystem::path current_executable_path(
        const char* fallback_path,
        std::error_code& error)
    {
        std::filesystem::path path;
#if defined(_WIN32)
        std::wstring buffer(32768u, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length != 0 && length < buffer.size())
        {
            buffer.resize(length);
            path = std::move(buffer);
        }
#elif defined(__APPLE__)
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        if (size != 0)
        {
            std::vector<char> buffer(size, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) == 0) path = buffer.data();
        }
#endif
        if (path.empty()) path = fallback_path;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
        if (!error) return normalized;
        error.clear();
        return std::filesystem::absolute(path, error);
    }
}

int main(int argument_count, char** arguments)
{
    std::optional<std::filesystem::path> explicit_toolchain_root;
    int command_index = 1;
    if (argument_count > 2 && std::string(arguments[1]) == "--toolchain-root")
    {
        explicit_toolchain_root = std::filesystem::path(arguments[2]);
        command_index = 3;
    }
    if (command_index >= argument_count)
    {
        print_usage();
        return 2;
    }

    const std::string command = arguments[command_index];
    if (command == "toolchain-info")
    {
        if (command_index + 1 != argument_count)
        {
            print_usage();
            return 2;
        }
        std::error_code error;
        const std::filesystem::path executable_path = current_executable_path(arguments[0], error);
        if (error)
        {
            std::cerr << "error: unable to resolve Toy3dShaderCompiler executable path: "
                      << error.message() << '\n';
            return 2;
        }
        const std::filesystem::path toolchain_root = explicit_toolchain_root.value_or(
            toy3d::shader::shader_toolchain_root_for_executable(executable_path));
        const toy3d::shader::ToolchainDiscoveryResult discovered =
            toy3d::shader::discover_shader_toolchain(toolchain_root);
        for (const toy3d::shader::Diagnostic& diagnostic : discovered.diagnostics)
        {
            std::cerr << severity_name(diagnostic.severity) << ": " << diagnostic.message << '\n';
        }
        if (!discovered.succeeded()) return 1;
        std::cout << "Shader toolchain root: " << toolchain_root.generic_string() << '\n'
                  << "Host platform: " << discovered.toolchain->manifest.host_platform << '\n'
                  << "Bundle identity: " << discovered.toolchain->manifest.identity << '\n';
        return 0;
    }

    if (command != "parse" || command_index + 2 != argument_count)
    {
        print_usage();
        return 2;
    }

    const std::string path = arguments[command_index + 1];
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
