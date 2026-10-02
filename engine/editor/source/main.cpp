#include "editor_host.h"

#include "config/command_line_parser.h"

namespace toy3d
{
    int run_application_host(void* native_instance)
    {
#if TOY3D_LINKED_GAME_MODULE
        auto& arguments = CommandLineParser::get_instance();
        if (!arguments.has_option("Project"))
        {
            arguments.parser_args({"Toy3d", std::string("--Project=") + TOY3D_PROJECT_DESCRIPTOR});
        }
        EditorHostConfig config;
        config.module = linked_game_module();
        config.game_executable = PhysicalPath(TOY3D_PROJECT_GAME_EXECUTABLE);
        return run_editor_host(native_instance, config);
#else
        return run_editor_host(native_instance);
#endif
    }
} // namespace toy3d
