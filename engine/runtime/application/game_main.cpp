#include "application/game_host.h"

#include "config/command_line_parser.h"
#include "config/console_manager.h"
#include "file_system/native_platform_file.h"
#include "platform/dynamic_library.h"
#include <iostream>

namespace toy3d
{
    int run_application_host(void* native_instance)
    {
        auto& arguments = CommandLineParser::get_instance();
        NativePlatformFile platform;
        const auto executable = executable_file_path();
        const auto root = executable.succeeded() ? platform.parent_path(executable.value())
                                                 : FileResult<PhysicalPath>(executable.status());
        if (!root.succeeded())
        {
            std::cerr << root.status().message << '\n';
            return 1;
        }
        const auto package_path = platform.join_relative(root.value(), "game_package.ini").value();
        const auto package = platform.read_text_utf8(package_path);
        if (package.succeeded())
        {
            const auto values = ConsoleManager::parse_config(package.value(), package_path.utf8());
            if (!values.succeeded() || values.value().size() != 2u ||
                values.value().count("Package.FormatVersion") != 1u ||
                values.value().at("Package.FormatVersion").value != "1" ||
                values.value().count("Package.Descriptor") != 1u || arguments.has_option("EditorShaderArtifacts"))
            {
                std::cerr << "Invalid game package manifest or development-only Shader option.\n";
                return 1;
            }
            const auto descriptor = platform.join_relative(root.value(), values.value().at("Package.Descriptor").value);
            if (!descriptor.succeeded())
            {
                std::cerr << descriptor.status().message << '\n';
                return 1;
            }
            GameHostPaths paths;
            paths.descriptor = descriptor.value();
            paths.deployment = root.value();
            paths.engine_assets = PhysicalPath(root.value().utf8() + "/engine/asset");
            paths.engine_config = PhysicalPath(root.value().utf8() + "/engine/config");
            paths.shader_deployment = PhysicalPath(root.value().utf8() + "/shader/player");
            return run_game_host(paths, linked_game_module(), native_instance);
        }
        if (package.status().code != FileErrorCode::NotFound)
        {
            std::cerr << package.status().message << '\n';
            return 1;
        }
        if (!arguments.has_option("Project"))
        {
            arguments.parser_args({"Toy3d", std::string("--Project=") + TOY3D_PROJECT_DESCRIPTOR});
        }
        GameHostPaths paths;
        paths.descriptor = PhysicalPath(arguments.get_option("Project"));
        paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
        paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
        paths.engine_config = PhysicalPath(TOY3D_EDITOR_ENGINE_CONFIG_ROOT);
        if (!arguments.has_option("EditorShaderArtifacts"))
        {
            paths.shader_deployment = PhysicalPath(TOY3D_GAME_SHADER_ROOT);
        }
        return run_game_host(paths, linked_game_module(), native_instance);
    }
} // namespace toy3d
