#include "compiler/toolchain_manifest.h"
#include "core/misc/logger.h"
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
    class LoggerLifetime
    {
    public:
        ~LoggerLifetime()
        {
            toy3d::Logger::get_instance().exit();
        }
    };

    void report_message(toy3d::Logger::Level level, const std::string& message)
    {
        std::cerr << message << '\n';
        switch (level)
        {
            case toy3d::Logger::Level::TOY_WARN:
                TOY_LOG_WARN("{}", message);
                break;
            case toy3d::Logger::Level::TOY_ERROR:
            case toy3d::Logger::Level::TOY_CRITICAL:
                TOY_LOG_ERROR("{}", message);
                break;
            default:
                TOY_LOG_INFO("{}", message);
                break;
        }
    }

    void report_diagnostic(const toy3d::shader::Diagnostic& diagnostic)
    {
        const std::string message = toy3d::shader::format_diagnostic(diagnostic);
        const toy3d::Logger::Level level =
            diagnostic.severity == toy3d::shader::DiagnosticSeverity::Warning
                ? toy3d::Logger::Level::TOY_WARN
                : toy3d::Logger::Level::TOY_ERROR;
        report_message(level, message);
    }

    void print_usage()
    {
        report_message(
            toy3d::Logger::Level::TOY_ERROR,
            "Usage:\n"
            "  Toy3dShaderCompiler [--toolchain-root <path>] parse <input.shader>\n"
            "  Toy3dShaderCompiler [--toolchain-root <path>] toolchain-info");
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
    toy3d::LogConfig log_config;
    log_config.logger_name = "Toy3dShaderCompiler";
    log_config.log_directory = TOY3D_SHADER_COMPILER_LOG_DIR;
    log_config.file_name = "shader_compiler.log";
    log_config.console_output = false;
    std::string log_error;
    if (!toy3d::Logger::get_instance().init(log_config, &log_error))
    {
        std::cerr << "warning: unable to initialize Shader compiler log: " << log_error << '\n';
    }
    const LoggerLifetime logger_lifetime;
    TOY_LOG_INFO("Shader compiler started.");

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
            report_message(
                toy3d::Logger::Level::TOY_ERROR,
                "error: unable to resolve Toy3dShaderCompiler executable path: " +
                    error.message());
            return 2;
        }
        const std::filesystem::path toolchain_root = explicit_toolchain_root.value_or(
            toy3d::shader::shader_toolchain_root_for_executable(executable_path));
        const toy3d::shader::ToolchainDiscoveryResult discovered =
            toy3d::shader::discover_shader_toolchain(toolchain_root);
        for (const toy3d::shader::Diagnostic& diagnostic : discovered.diagnostics)
        {
            report_diagnostic(diagnostic);
        }
        if (!discovered.succeeded()) return 1;
        TOY_LOG_INFO(
            "Shader toolchain discovery succeeded for '{}'.",
            toolchain_root.generic_string());
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
        report_message(
            toy3d::Logger::Level::TOY_ERROR,
            path + ": error: unable to open Shader asset.");
        return 2;
    }
    const std::string source{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const toy3d::shader::ParseResult result = toy3d::shader::parse_shader(source, path);
    for (const toy3d::shader::Diagnostic& diagnostic : result.diagnostics)
    {
        report_diagnostic(diagnostic);
    }
    if (!result.succeeded())
    {
        return 1;
    }

    const toy3d::shader::ShaderAsset& asset = *result.asset;
    TOY_LOG_INFO("Parsed Shader '{}' from '{}'.", asset.name, path);
    std::cout << "Parsed Shader '" << asset.name << "' (v" << asset.version << ") with "
              << asset.properties.size() << " properties, " << asset.resources.size()
              << " resources, " << asset.variants.size() << " variants, and "
              << asset.passes.size() << " passes.\n";
    return 0;
}
