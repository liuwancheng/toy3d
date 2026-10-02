#include "application/game_host.h"

#include "config/command_line_parser.h"

namespace toy3d
{
    int run_application_host(void* native_instance)
    {
        auto& arguments = CommandLineParser::get_instance();
        if (!arguments.has_option("Project")) arguments.parser_args({"Toy3d", std::string("--Project=") + TOY3D_PROJECT_DESCRIPTOR});
        GameHostPaths paths;
        paths.descriptor = PhysicalPath(arguments.get_option("Project"));
        paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
        paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
        paths.engine_config = PhysicalPath(TOY3D_EDITOR_ENGINE_CONFIG_ROOT);
        return run_game_host(paths, linked_game_module(), native_instance);
    }
}
