#include "shader_map/shader_map_entry.h"
#include "codegen/shader_parameters_codegen.h"
#include "codegen/shader_parameters_writer.h"
#include "compiler/program_compiler.h"
#include "compiler/standard_surface.h"
#include "compiler/toolchain_manifest.h"
#include "logging/logger.h"
#include "file_system/native_platform_file.h"
#include "file_system/directory_file_store.h"
#include "file_system/file_system.h"
#include "frontend/shader_parser.h"

#include <cstdint>
#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "platform/platform_defines.h"
#include "shader/shader_build_settings.h"
#include "shader_map/shader_deployment_cook.h"

#if WITH_WIN
#include <Windows.h>
#elif WITH_MAC
#include <mach-o/dyld.h>
#endif

namespace
{
    // optional records whether the CLI supplied a toolchain root, avoiding an
    // empty path sentinel that could be confused with a real argument.
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
                       "  Toy3dShaderCompiler generate-parameters <output-directory> <input.shader>...\n"
                       "  Toy3dShaderCompiler [--toolchain-root <path>] compile-vulkan <input.shader> <virtual-path> "
                       "<pass> <shader-map-root> <working-directory> [--variant <name>=<value>]... [--request <path>] "
                       "[--settings <path>] [--project-settings <path>] [--build-mode Editor|Player] "
                       "[--engine-include-root <path>] [--project-include-root <path>]\n"
                       "  Toy3dShaderCompiler [--toolchain-root <path>] cook-vulkan <engine-root> <project-root|-> "
                       "<new-deployment-root> <new-work-root> Editor|Player\n"
                       "  Toy3dShaderCompiler [--toolchain-root <path>] toolchain-info");
    }

    toy3d::FileResult<toy3d::PhysicalPath> current_executable_path(const toy3d::PlatformFile& platform_file,
                                                                   const char* fallback_path, std::string& error)
    {
        toy3d::PhysicalPath path;
#if WITH_WIN
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
#elif WITH_MAC
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        if (size != 0)
        {
            std::vector<char> buffer(size, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) == 0)
            {
                path = toy3d::PhysicalPath(buffer.data());
            }
        }
#endif
        if (path.empty())
        {
            path = toy3d::PhysicalPath(fallback_path);
        }
        toy3d::FileResult<toy3d::PhysicalPath> normalized = platform_file.canonical(path);
        if (normalized.succeeded())
        {
            return normalized;
        }
        normalized = platform_file.absolute(path);
        if (!normalized.succeeded())
        {
            error = normalized.status().message;
        }
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
    if (command == "cook-vulkan")
    {
        if (command_index + 6 != argument_count)
        {
            print_usage();
            return 2;
        }
        const std::string mode = arguments[command_index + 5];
        if (mode != "Editor" && mode != "Player")
        {
            print_usage();
            return 2;
        }
        std::string error;
        const auto executable = current_executable_path(platform_file, arguments[0], error);
        if (!executable.succeeded())
        {
            report_message(toy3d::Logger::Level::TOY_ERROR, error);
            return 2;
        }
        toy3d::shader::ShaderDeploymentCookInput input;
        input.engine_root = toy3d::PhysicalPath(arguments[command_index + 1]);
        const std::string project = arguments[command_index + 2];
        input.project_root = project == "-" ? toy3d::PhysicalPath{} : toy3d::PhysicalPath(project);
        input.output_root = toy3d::PhysicalPath(arguments[command_index + 3]);
        input.work_root = toy3d::PhysicalPath(arguments[command_index + 4]);
        input.compiler = executable.value();
        input.editor = mode == "Editor";
        if (explicit_toolchain_root)
        {
            input.toolchain = *explicit_toolchain_root;
        }
        else
        {
            const auto root = toy3d::shader::shader_toolchain_root_for_executable(platform_file, executable.value());
            if (!root.succeeded())
            {
                report_message(toy3d::Logger::Level::TOY_ERROR, root.status().message);
                return 2;
            }
            input.toolchain = root.value();
        }
        if (!toy3d::shader::cook_shader_deployment(platform_file, toy3d::NativeProcessService{}, input, error))
        {
            report_message(toy3d::Logger::Level::TOY_ERROR, error);
            return 1;
        }
        return 0;
    }
    if (command == "generate-parameters")
    {
        if (explicit_toolchain_root || command_index + 3 > argument_count)
        {
            print_usage();
            return 2;
        }
        std::vector<toy3d::shader::ShaderParametersGeneratedUnit> units;
        toy3d::shader::ShaderParametersGeneratedUnit builtin;
        builtin.header = toy3d::shader::generate_builtin_shader_parameters_header();
        builtin.dependencies.push_back("builtin://GlobalViewObject");
        units.push_back(std::move(builtin));
        for (int index = command_index + 2; index < argument_count; ++index)
        {
            const std::string input_path = arguments[index];
            const toy3d::FileResult<std::string> input = platform_file.read_text_utf8(toy3d::PhysicalPath(input_path));
            if (!input.succeeded())
            {
                report_message(toy3d::Logger::Level::TOY_ERROR,
                               input_path + ": error: unable to read Shader parameter source.");
                return 2;
            }
            const toy3d::shader::ParseResult parsed = toy3d::shader::parse_shader(input.value(), input_path);
            for (const toy3d::shader::Diagnostic& diagnostic : parsed.diagnostics)
            {
                report_diagnostic(diagnostic);
            }
            if (!parsed.succeeded())
            {
                return 1;
            }
            const toy3d::shader::LogicalLayoutResult layout = toy3d::shader::compile_logical_layout(*parsed.asset);
            for (const toy3d::shader::Diagnostic& diagnostic : layout.diagnostics)
            {
                report_diagnostic(diagnostic);
            }
            if (!layout.succeeded())
            {
                return 1;
            }
            toy3d::shader::ShaderParametersGeneratedUnit unit;
            unit.header = toy3d::shader::generate_shader_parameters_header(*parsed.asset, *layout.layout);
            for (const toy3d::shader::Diagnostic& diagnostic : unit.header.diagnostics)
            {
                report_diagnostic(diagnostic);
            }
            if (!unit.header.succeeded())
            {
                return 1;
            }
            unit.dependencies.push_back(input_path);
            units.push_back(std::move(unit));
        }
        const toy3d::shader::ShaderParametersWriteResult written = toy3d::shader::write_shader_parameter_headers(
            platform_file, toy3d::PhysicalPath(arguments[command_index + 1]), units);
        for (const toy3d::shader::Diagnostic& diagnostic : written.diagnostics)
        {
            report_diagnostic(diagnostic);
        }
        if (!written.succeeded())
        {
            return 1;
        }
        std::cout << "Generated " << written.outputs.size() << " Shader parameters header(s); "
                  << written.changed_outputs.size() << " changed and " << written.removed_outputs.size()
                  << " stale output(s) removed.\n";
        return 0;
    }
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
        {
            return 1;
        }
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
    const std::string diagnostic_path = compile_vulkan ? arguments[command_index + 2] : path;
    const toy3d::shader::ParseResult result = toy3d::shader::parse_shader(source, diagnostic_path);
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
        {
            report_diagnostic(diagnostic);
        }
        if (!discovered.succeeded())
        {
            return 1;
        }

        toy3d::shader::ShaderProgramCompileInput compile_input;
        compile_input.source_virtual_path = arguments[command_index + 2];
        compile_input.pass_name = arguments[command_index + 3];
        toy3d::shader::ShaderSourceCompileRequest source_request;
        bool request_supplied = false;
        toy3d::PhysicalPath settings_path, project_settings_path;
        bool build_editor = true;
        bool build_mode_supplied = false;
        toy3d::FileSystem includes;
        bool engine_include = false, project_include = false;
        for (int index = compile_required_end; index < argument_count; index += 2)
        {
            const std::string option = arguments[index];
            if (option == "--settings" || option == "--project-settings")
            {
                auto& path = option == "--settings" ? settings_path : project_settings_path;
                if (!path.empty())
                {
                    print_usage();
                    return 2;
                }
                path = toy3d::PhysicalPath(arguments[index + 1]);
                continue;
            }
            else if (option == "--build-mode")
            {
                const std::string mode = arguments[index + 1];
                if (build_mode_supplied || (mode != "Editor" && mode != "Player"))
                {
                    print_usage();
                    return 2;
                }
                build_mode_supplied = true;
                build_editor = mode == "Editor";
                continue;
            }
            else if (option == "--request")
            {
                if (request_supplied)
                {
                    print_usage();
                    return 2;
                }
                request_supplied = true;
                const toy3d::PhysicalPath request_path(arguments[index + 1]);
                const auto stat = platform_file.stat(request_path);
                if (!stat.succeeded() || stat.value().type != toy3d::FileType::File ||
                    stat.value().size > toy3d::shader::max_shader_source_compile_request_bytes)
                {
                    report_message(toy3d::Logger::Level::TOY_ERROR, "Invalid compile request file or read budget.");
                    return 2;
                }
                const auto text = platform_file.read_text_utf8(request_path);
                if (!text.succeeded() ||
                    !toy3d::shader::parse_shader_source_compile_request(text.value(), source_request, error))
                {
                    report_message(toy3d::Logger::Level::TOY_ERROR, text.succeeded() ? error : text.status().message);
                    return 2;
                }
                continue;
            }
            if (option == "--engine-include-root" || option == "--project-include-root")
            {
                bool& supplied = option == "--engine-include-root" ? engine_include : project_include;
                if (supplied)
                {
                    print_usage();
                    return 2;
                }
                supplied = true;
                toy3d::DirectoryFileStoreDesc desc;
                desc.physical_root = toy3d::PhysicalPath(arguments[index + 1]);
                const auto store = toy3d::DirectoryFileStore::create(platform_file, desc);
                if (!store.succeeded())
                {
                    report_message(toy3d::Logger::Level::TOY_ERROR, store.status().message);
                    return 2;
                }
                toy3d::FileMountDesc mount;
                const auto root = toy3d::VirtualPath::parse(
                    option == "--engine-include-root" ? "/Engine/ShaderIncludes" : "/Project/ShaderIncludes");
                if (!root.succeeded())
                {
                    report_message(toy3d::Logger::Level::TOY_ERROR, root.status().message);
                    return 2;
                }
                mount.virtual_root = root.value();
                mount.store = store.value();
                const auto added = includes.add_mount(mount);
                if (!added.succeeded())
                {
                    report_message(toy3d::Logger::Level::TOY_ERROR, added.message);
                    return 2;
                }
                continue;
            }
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
        const auto frozen = includes.freeze();
        if (!frozen.succeeded())
        {
            report_message(toy3d::Logger::Level::TOY_ERROR, frozen.message);
            return 2;
        }
        const toy3d::shader::FileShaderSourceProvider source_provider(includes);
        compile_input.source_provider = &source_provider;
        const auto requested_pass = std::find_if(result.asset->passes.begin(), result.asset->passes.end(),
                                                 [&](const toy3d::shader::ShaderPass& pass)
                                                 {
                                                     return pass.name == compile_input.pass_name;
                                                 });
        if (requested_pass == result.asset->passes.end())
        {
            report_message(toy3d::Logger::Level::TOY_ERROR, "Requested Pass is not declared by this source.");
            return 1;
        }
        if ((request_supplied && !settings_path.empty()) ||
            (settings_path.empty() && (!project_settings_path.empty() || build_mode_supplied)))
        {
            report_message(toy3d::Logger::Level::TOY_ERROR,
                           "Build settings require --settings and cannot be combined with --request.");
            return 2;
        }
        if (request_supplied && !compile_input.variant_selections.empty())
        {
            report_message(toy3d::Logger::Level::TOY_ERROR, "--request and --variant cannot be combined.");
            return 2;
        }
        if (source_request.policy.target != toy3d::shader::ShaderTarget::VulkanSpirV ||
            source_request.policy.profile != toy3d::shader::ShaderCompileProfile::VulkanES31)
        {
            report_message(toy3d::Logger::Level::TOY_ERROR, "compile-vulkan requires Vulkan ES3.1 policy.");
            return 2;
        }
        const auto domain = toy3d::shader::shader_material_domain(*result.asset);
        if (!request_supplied)
        {
            const auto material =
                toy3d::shader::resolve_shader_permutation(*result.asset, compile_input.variant_selections);
            if (!material.succeeded())
            {
                for (const auto& diagnostic : material.diagnostics)
                {
                    report_diagnostic(diagnostic);
                }
                return 1;
            }
            source_request.configurations = {material.permutation->selections};
        }
        if (!settings_path.empty())
        {
            toy3d::shader::ShaderBuildSettings settings;
            if (!toy3d::shader::read_shader_build_settings(platform_file, settings_path, project_settings_path,
                                                           settings, error) ||
                !toy3d::shader::make_shader_source_compile_request(
                    settings, result.asset->name, toy3d::shader::ShaderTarget::VulkanSpirV,
                    toy3d::shader::ShaderCompileProfile::VulkanES31, build_editor,
                    std::move(source_request.configurations), source_request, error))
            {
                report_message(toy3d::Logger::Level::TOY_ERROR, error);
                return 2;
            }
        }
        std::vector<toy3d::shader::ShaderMapIndex> indices;
        std::vector<toy3d::shader::ShaderCompilePlan> plans;
        std::vector<std::vector<toy3d::shader::ShaderVariantSelection>> selections;
        std::set<toy3d::Sha256Hash> configuration_keys;
        std::size_t declared_total = 0u, required_total = 0u, filtered_total = 0u, upper_total = 0u;
        // Normalize, expand and budget every configuration before invoking DXC.
        // Standard Masked adds roles, so counting only the unexpanded AST is insufficient.
        for (const auto& requested : source_request.configurations)
        {
            const auto material = toy3d::shader::resolve_shader_permutation(domain, requested);
            if (!material.succeeded())
            {
                report_message(toy3d::Logger::Level::TOY_ERROR, material.errors.front().message);
                return 1;
            }
            if (!configuration_keys.insert(material.permutation->key).second)
            {
                continue;
            }
            std::vector<toy3d::shader::ShaderVariantSelection> variants;
            for (const auto& selection : material.permutation->selections)
            {
                variants.push_back({selection.name, selection.kind == toy3d::shader::ShaderPermutationValueKind::Boolean
                                                        ? (selection.boolean_value ? "true" : "false")
                                                        : selection.enum_value});
            }
            toy3d::shader::ShaderAsset expanded;
            std::vector<toy3d::shader::Diagnostic> diagnostics;
            if (!toy3d::shader::expand_standard_surface(*result.asset, variants, expanded, diagnostics))
            {
                for (const auto& diagnostic : diagnostics)
                {
                    report_diagnostic(diagnostic);
                }
                return 1;
            }
            auto plan =
                toy3d::shader::plan_shader_compilation(toy3d::shader::shader_compile_source(expanded),
                                                       {material.permutation->selections}, source_request.policy);
            if (!plan.succeeded())
            {
                report_message(toy3d::Logger::Level::TOY_ERROR, plan.error);
                return 1;
            }
            std::size_t engine_upper = 1u;
            for (const auto& feature : expanded.features)
            {
                if (feature.feature == toy3d::shader::ShaderEngineFeature::Shadows ||
                    feature.feature == toy3d::shader::ShaderEngineFeature::Environment)
                {
                    engine_upper *= 2u;
                }
            }
            const std::size_t factories =
                expanded.usage == toy3d::shader::ShaderUsage::Global
                    ? 1u
                    : (toy3d::shader::supports_vertex_factory(expanded.vertex_factory_support,
                                                              toy3d::shader::VertexFactoryType::Local)
                           ? 1u
                           : 0u) +
                          (toy3d::shader::supports_vertex_factory(expanded.vertex_factory_support,
                                                                  toy3d::shader::VertexFactoryType::GPUSkin)
                               ? 1u
                               : 0u);
            const auto upper = expanded.passes.size() * factories * engine_upper;
            if (upper > toy3d::shader::max_shader_compile_source_programs - upper_total)
            {
                report_message(toy3d::Logger::Level::TOY_ERROR,
                               "Source configuration product exceeds 1024 programs before filtering.");
                return 1;
            }
            upper_total += upper;
            if (plan.declared_programs > toy3d::shader::max_shader_compile_source_programs - declared_total ||
                plan.required.size() > toy3d::shader::max_shader_compile_source_programs - required_total)
            {
                report_message(
                    toy3d::Logger::Level::TOY_ERROR,
                    "Source job exceeds 1024 programs: configurations x expanded roles x factories x engine options.");
                return 1;
            }
            declared_total += plan.declared_programs;
            required_total += plan.required.size();
            filtered_total += plan.filtered_programs;
            toy3d::shader::ShaderMapIndex map_index;
            map_index.shader_name = result.asset->name;
            map_index.source_hash = toy3d::sha256(source);
            map_index.material_domain = domain;
            map_index.material_selections = material.permutation->selections;
            map_index.permutation_key = material.permutation->key;
            map_index.policy = source_request.policy;
            map_index.features = result.asset->features;
            map_index.supported_when = result.asset->supported_when;
            map_index.standard_tangent_input = result.asset->standard_tangent_input;
            map_index.declares_tangent_frame = result.asset->declares_tangent_frame;
            map_index.tangent_frame_when = result.asset->tangent_frame_when;
            for (const auto& pass : expanded.passes)
            {
                map_index.passes.push_back({pass.name, pass.role});
            }
            indices.push_back(std::move(map_index));
            plans.push_back(std::move(plan));
            selections.push_back(std::move(variants));
        }
        const toy3d::PhysicalPath output_root(arguments[command_index + 4]);
        const toy3d::PhysicalPath work_root(arguments[command_index + 5]);
        toy3d::shader::ShaderStageCompileCache stage_cache;
        compile_input.stage_cache = &stage_cache;
        std::size_t program_number = 0u;
        for (std::size_t configuration = 0u; configuration < plans.size(); ++configuration)
        {
            auto& map_index = indices[configuration];
            compile_input.variant_selections = selections[configuration];
            for (const auto& required : plans[configuration].required)
            {
                compile_input.pass_name = required.pass.name;
                compile_input.vertex_factory = required.vertex_factory;
                compile_input.pass_selections = required.pass_permutation.selections;
                compile_input.compile_policy = source_request.policy;
                const auto work = platform_file.join_relative(work_root, "program_" + std::to_string(program_number));
                if (!work.succeeded())
                {
                    report_message(toy3d::Logger::Level::TOY_ERROR, work.status().message);
                    return 1;
                }
                const auto compiled = toy3d::shader::compile_vulkan_shader_map_entry(
                    *result.asset, compile_input, *discovered.toolchain, platform_file, work.value());
                for (const auto& diagnostic : compiled.diagnostics)
                {
                    report_diagnostic(diagnostic);
                }
                if (!compiled.succeeded())
                {
                    return 1;
                }
                const auto written = toy3d::shader::write_verified_shader_map_entry(
                    platform_file, output_root, *compiled.entry, compiled.editor_properties);
                for (const auto& diagnostic : written.diagnostics)
                {
                    report_diagnostic(diagnostic);
                }
                if (!written.succeeded())
                {
                    return 1;
                }
                map_index.programs.push_back({required.pass.name, compiled.entry->contract, written.shader_map_key,
                                              written.entry_content_hash, required.pass_permutation.key,
                                              required.pass_permutation.selections});
                std::cout << "Compiled ShaderMapEntry '" << compiled.entry->shader_name << "/" << required.pass.name
                          << "' to " << written.entry_directory->utf8() << '\n';
                ++program_number;
            }
        }
        std::cout << "Stages: compiled VS=" << stage_cache.compiled[0] << " PS=" << stage_cache.compiled[1]
                  << " CS=" << stage_cache.compiled[2] << ", reused VS=" << stage_cache.reused[0]
                  << " PS=" << stage_cache.reused[1] << " CS=" << stage_cache.reused[2] << '\n';
        // No index is published until every configuration's entries have compiled.
        // Consumers publish the containing immutable request directory as one revision.
        for (const auto& index : indices)
        {
            if (!toy3d::shader::write_verified_shader_map_index(platform_file, output_root, index, error))
            {
                report_message(toy3d::Logger::Level::TOY_ERROR, "ShaderMap index publication failed: " + error);
                return 1;
            }
        }
        std::cout << "Published " << indices.size() << " configuration(s), " << required_total << " program(s).\n";
        std::cout << "Plan: declared=" << declared_total << ", required=" << required_total
                  << ", filtered=" << filtered_total << '\n';
        return 0;
    }

    const toy3d::shader::ShaderAsset& asset = *result.asset;
    TOY_LOG_INFO("Parsed Shader '{}' from '{}'.", asset.name, path);
    std::cout << "Parsed Shader '" << asset.name << "' (v" << asset.version << ") with " << asset.properties.size()
              << " properties, " << asset.resources.size() << " resources, " << asset.variants.size()
              << " variants, and " << asset.passes.size() << " passes.\n";
    return 0;
}
