#include "cube_application.h"

#include "config/command_line_parser.h"
#include "engine.h"
#include "logging/logger.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    bool build_cube_test_arguments(
        int argc,
        char* argv[],
        std::vector<std::string>& arguments,
        bool& automated_window_events,
        bool& auto_close)
    {
        automated_window_events = false;
        auto_close = false;
        bool rendering_mode_overridden = false;
        arguments.clear();
        arguments.reserve(static_cast<std::size_t>(argc) + 1u);
        for (int index = 0; index < argc; ++index)
        {
            const std::string argument = argv[index] != nullptr
                ? argv[index]
                : std::string();
            constexpr const char* k_renderer_multithreaded_prefix =
                "--Renderer.MultiThreaded=";
            constexpr const char* k_plain_renderer_multithreaded_prefix =
                "Renderer.MultiThreaded=";
            if (argument.rfind(k_renderer_multithreaded_prefix, 0u) == 0u ||
                argument.rfind(
                    k_plain_renderer_multithreaded_prefix, 0u) == 0u)
            {
                rendering_mode_overridden = true;
            }

            constexpr const char* k_mode_prefix = "--cube-rendering-mode=";
            if (argument.rfind(k_mode_prefix, 0u) == 0u)
            {
                const std::string mode =
                    argument.substr(std::string(k_mode_prefix).size());
                if (mode == "single")
                {
                    arguments.push_back("--Renderer.MultiThreaded=false");
                    rendering_mode_overridden = true;
                    continue;
                }
                if (mode == "multi")
                {
                    arguments.push_back("--Renderer.MultiThreaded=true");
                    rendering_mode_overridden = true;
                    continue;
                }

                std::cerr << "Unknown cube rendering mode '" << mode
                          << "'. Expected 'single' or 'multi'.\n";
                return false;
            }

            constexpr const char* k_window_events_prefix =
                "--cube-window-events=";
            if (argument.rfind(k_window_events_prefix, 0u) == 0u)
            {
                const std::string events = argument.substr(
                    std::string(k_window_events_prefix).size());
                if (events == "resize-minimize-restore")
                {
                    automated_window_events = true;
                    continue;
                }

                std::cerr << "Unknown cube window event mode '" << events
                          << "'. Expected 'resize-minimize-restore'.\n";
                return false;
            }

            constexpr const char* k_auto_close_prefix =
                "--cube-auto-close=";
            if (argument.rfind(k_auto_close_prefix, 0u) == 0u)
            {
                const std::string close_mode = argument.substr(
                    std::string(k_auto_close_prefix).size());
                if (close_mode == "true" || close_mode == "on" ||
                    close_mode == "1")
                {
                    auto_close = true;
                    continue;
                }
                if (close_mode == "false" || close_mode == "off" ||
                    close_mode == "0")
                {
                    auto_close = false;
                    continue;
                }

                std::cerr << "Unknown cube auto close mode '" << close_mode
                          << "'. Expected true/on/1 or false/off/0.\n";
                return false;
            }

            arguments.push_back(argument);
        }
        if (!rendering_mode_overridden)
        {
            arguments.push_back("--Renderer.MultiThreaded=true");
        }
        return true;
    }
}

int main(int argc, char* argv[])
{
    std::vector<std::string> command_line;
    bool automated_window_events = false;
    bool auto_close = false;
    if (!build_cube_test_arguments(
            argc, argv, command_line, automated_window_events, auto_close))
    {
        return 1;
    }
    toy3d::CommandLineParser::get_instance().parser_args(command_line);

    toy3d::Engine engine;
    toy3d::ShaderLoadConfig shader_config;
    shader_config.mode = toy3d::ShaderLoadMode::ShaderMapEntry;
    shader_config.path = toy3d::PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT);
    engine.set_shader_load_config(std::move(shader_config));

    auto application = std::make_unique<CubeApplication>(
        automated_window_events, auto_close);
    CubeApplication* const application_observer = application.get();
    engine.set_application(std::move(application));

    engine.init(nullptr);
    TOY_LOG_INFO("Toy3dCubeTest started.");
    engine.main_loop();
    engine.exit();
    return application_observer->setup_failed() ? 1 : 0;
}
