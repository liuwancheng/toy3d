#include "shader_map/shader_map_entry.h"
#include "compiler/program_compiler.h"
#include "compiler/toolchain_manifest.h"
#include "logging/logger.h"
#include "file_system/native_platform_file.h"
#include "frontend/shader_parser.h"

#include <cstdint>
#include <iostream>
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
    // optional records whether the CLI supplied a toolchain root, avoiding an
    // empty path sentinel that could be confused with a real argument.
    class LoggerLifetime
    {
      public:
        ~LoggerLifetime() { toy3d::Logger::get_instance().exit(); }
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
        const toy3d::Logger::Level level = diagnostic.severity == toy3d::shader::DiagnosticSeverity::Warning
                                               ? toy3d::Logger::Level::TOY_WARN
                                               : toy3d::Logger::Level::TOY_ERROR;
        report_message(level, message);
    }

    void print_usage()
    {
        report_message(toy3d::Logger::Level::TOY_ERROR,
                       "Usage:\n"
                       "  Toy3dShaderCompiler [--toolchain-root <path>] parse <input.shader>\n"
                       "  Toy3dShaderCompiler [--toolchain-root <path>] compile-vulkan <input.shader> <virtual-path> "
                       "<pass> <shader-map-root> <working-directory> [--variant <name>=<value>]...\n"
                       "  Toy3dShaderCompiler [--toolchain-root <path>] toolchain-info");
    }

    toy3d::FileResult<toy3d::PhysicalPath> current_executable_path(const toy3d::PlatformFile& platform_file,
                                                                   const char* fallback_path, std::string& error)
    {
        toy3d::PhysicalPath path;
#if defined(_WIN32)
        std::wstring buffer(32768u, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length != 0 && length < buffer.size())
        {
            buffer.resize(length);
            const int utf8_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(),
                                                      static_cast<int>(buffer.size()), nullptr, 0, nullptr, nullptr);
            if (utf8_size > 0)
            {
                std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
                if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), static_cast<int>(buffer.size()),
                                        utf8.data(), utf8_size, nullptr, nullptr) == utf8_size)
                {
                    path = toy3d::PhysicalPath(std::move(utf8));
                }
            }
        }
#elif defined(__APPLE__)
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        if (size != 0)
        {
            std::vector<char> buffer(size, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) == 0)
                path = toy3d::PhysicalPath(buffer.data());
        }
#endif
        if (path.empty())
            path = toy3d::PhysicalPath(fallback_path);
        toy3d::FileResult<toy3d::PhysicalPath> normalized = platform_file.canonical(path);
        if (normalized.succeeded())
            return normalized;
        normalized = platform_file.absolute(path);
        if (!normalized.succeeded())
            error = normalized.status().message;
        return normalized;
    }
} // namespace

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
    toy3d::NativePlatformFile platform_file;

    std::optional<toy3d::PhysicalPath> explicit_toolchain_root;
    int command_index = 1;
    if (argument_count > 2 && std::string(arguments[1]) == "--toolchain-root")
    {
        explicit_toolchain_root = toy3d::PhysicalPath(arguments[2]);
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
        std::string error;
        const toy3d::FileResult<toy3d::PhysicalPath> executable_path =
            current_executable_path(platform_file, arguments[0], error);
        if (!executable_path.succeeded())
        {
            report_message(toy3d::Logger::Level::TOY_ERROR,
                           "error: unable to resolve Toy3dShaderCompiler executable path: " + error);
            return 2;
        }
        toy3d::PhysicalPath toolchain_root;
        if (explicit_toolchain_root)
        {
            toolchain_root = *explicit_toolchain_root;
        }
        else
        {
            const toy3d::FileResult<toy3d::PhysicalPath> default_root =
                toy3d::shader::shader_toolchain_root_for_executable(platform_file, executable_path.value());
            if (!default_root.succeeded())
            {
                report_message(toy3d::Logger::Level::TOY_ERROR,
                               "error: unable to resolve default Shader toolchain root: " +
                                   default_root.status().message);
                return 2;
            }
            toolchain_root = default_root.value();
        }
        const toy3d::shader::ToolchainDiscoveryResult discovered =
            toy3d::shader::discover_shader_toolchain(platform_file, toolchain_root);
        for (const toy3d::shader::Diagnostic& diagnostic : discovered.diagnostics)
        {
            report_diagnostic(diagnostic);
        }
        if (!discovered.succeeded())
            return 1;
        TOY_LOG_INFO("Shader toolchain discovery succeeded for '{}'.", toolchain_root.utf8());
        std::cout << "Shader toolchain root: " << toolchain_root.utf8() << '\n'
                  << "Host platform: " << discovered.toolchain->manifest.host_platform << '\n'
                  << "Bundle identity: " << discovered.toolchain->manifest.identity << '\n';
        return 0;
    }

    const bool compile_vulkan = command == "compile-vulkan";
    const int compile_required_end = command_index + 6;
    const bool valid_compile_arguments =
        compile_vulkan && argument_count >= compile_required_end && (argument_count - compile_required_end) % 2 == 0;
    if ((!compile_vulkan && command != "parse") ||
        (compile_vulkan ? !valid_compile_arguments : command_index + 2 != argument_count))
    {
        print_usage();
        return 2;
    }

    const std::string path = arguments[command_index + 1];
    const toy3d::FileResult<std::string> source_file = platform_file.read_text_utf8(toy3d::PhysicalPath(path));
    if (!source_file.succeeded())
    {
        report_message(toy3d::Logger::Level::TOY_ERROR, path + ": error: unable to open Shader asset.");
        return 2;
    }
    const std::string& source = source_file.value();
    const toy3d::shader::ParseResult result = toy3d::shader::parse_shader(source, path);
    for (const toy3d::shader::Diagnostic& diagnostic : result.diagnostics)
    {
        report_diagnostic(diagnostic);
    }
    if (!result.succeeded())
    {
        return 1;
    }

    if (compile_vulkan)
    {
        std::string error;
        const toy3d::FileResult<toy3d::PhysicalPath> executable_path =
            current_executable_path(platform_file, arguments[0], error);
        if (!executable_path.succeeded())
        {
            report_message(toy3d::Logger::Level::TOY_ERROR,
                           "error: unable to resolve Toy3dShaderCompiler executable path: " + error);
            return 2;
        }
        toy3d::PhysicalPath toolchain_root;
        if (explicit_toolchain_root)
        {
            toolchain_root = *explicit_toolchain_root;
        }
        else
        {
            const toy3d::FileResult<toy3d::PhysicalPath> default_root =
                toy3d::shader::shader_toolchain_root_for_executable(platform_file, executable_path.value());
            if (!default_root.succeeded())
            {
                report_message(toy3d::Logger::Level::TOY_ERROR,
                               "error: unable to resolve default Shader toolchain root: " +
                                   default_root.status().message);
                return 2;
            }
            toolchain_root = default_root.value();
        }
        toy3d::shader::ToolchainDiscoveryResult discovered =
            toy3d::shader::discover_shader_toolchain(platform_file, toolchain_root);
        for (const toy3d::shader::Diagnostic& diagnostic : discovered.diagnostics)
            report_diagnostic(diagnostic);
        if (!discovered.succeeded())
            return 1;

        toy3d::shader::ShaderProgramCompileInput compile_input;
        compile_input.source_virtual_path = arguments[command_index + 2];
        compile_input.pass_name = arguments[command_index + 3];
        for (int index = compile_required_end; index < argument_count; index += 2)
        {
            if (std::string(arguments[index]) != "--variant")
            {
                print_usage();
                return 2;
            }
            const std::string selection = arguments[index + 1];
            const std::size_t separator = selection.find('=');
            if (separator == std::string::npos || separator == 0u || separator + 1u == selection.size())
            {
                report_message(toy3d::Logger::Level::TOY_ERROR, "error: --variant requires <name>=<value>.");
                return 2;
            }
            compile_input.variant_selections.push_back(
                {selection.substr(0, separator), selection.substr(separator + 1u)});
        }
        const toy3d::shader::RegisteredShaderSourceProvider source_provider({});
        compile_input.source_provider = &source_provider;
        toy3d::shader::ShaderMapEntryCompileResult compiled = toy3d::shader::compile_vulkan_shader_map_entry(
            *result.asset, compile_input, *discovered.toolchain, platform_file,
            toy3d::PhysicalPath(arguments[command_index + 5]));
        for (const toy3d::shader::Diagnostic& diagnostic : compiled.diagnostics)
            report_diagnostic(diagnostic);
        if (!compiled.succeeded())
            return 1;
        toy3d::shader::ShaderMapEntryWriteResult written = toy3d::shader::write_verified_shader_map_entry(
            platform_file, toy3d::PhysicalPath(arguments[command_index + 4]), *compiled.entry);
        for (const toy3d::shader::Diagnostic& diagnostic : written.diagnostics)
            report_diagnostic(diagnostic);
        if (!written.succeeded())
            return 1;
        std::cout << "Compiled ShaderMapEntry '" << compiled.entry->shader_name << "/" << compiled.entry->pass_name
                  << "' to " << written.entry_directory->utf8() << '\n';
        return 0;
    }

    const toy3d::shader::ShaderAsset& asset = *result.asset;
    TOY_LOG_INFO("Parsed Shader '{}' from '{}'.", asset.name, path);
    std::cout << "Parsed Shader '" << asset.name << "' (v" << asset.version << ") with " << asset.properties.size()
              << " properties, " << asset.resources.size() << " resources, " << asset.variants.size()
              << " variants, and " << asset.passes.size() << " passes.\n";
    return 0;
}
