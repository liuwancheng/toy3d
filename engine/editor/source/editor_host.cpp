#include "editor_host.h"
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <utility>

#include "platform/platform_defines.h"

#if WITH_WIN
#include <windows.h>
#endif

#include "config/command_line_parser.h"
#include "engine.h"
#include "editor.h"
#include "workspace/editor_workspace.h"
#include "logging/logger.h"
#include "platform/platform_services.h"
#include "workspace/editor_project.h"

int toy3d::run_editor_host(void* hInstance, const EditorHostConfig& host)
{
    Engine g_engine;
    ActorTypeRegistry actor_types;
    auto& arguments = CommandLineParser::get_instance();
    const PhysicalPath editor_directory(TOY3D_EDITOR_DEPLOY_ROOT);
    auto project = std::make_unique<EditorProject>(editor_directory, host.module.name);
    std::string project_error;
    if (arguments.has_option("Project"))
    {
        const auto opened = project->open(PhysicalPath(arguments.get_option("Project")));
        if (!opened.succeeded())
        {
            project_error = opened.message;
            project = std::make_unique<EditorProject>(editor_directory, host.module.name);
        }
    }
    NativePlatformFile platform;
    PhysicalPath saved;
    if (project->active())
    {
        saved = project->saved();
    }
    else
    {
        const auto user = user_data_directory();
        if (!user.succeeded())
        {
            std::cerr << user.status().message << '\n';
            return 1;
        }
        const auto path = platform.join_relative(user.value(), "Toy3d/Editor");
        if (!path.succeeded())
        {
            std::cerr << path.status().message << '\n';
            return 1;
        }
        saved = path.value();
    }
    const auto made = platform.create_directories(saved);
    if (!made.succeeded())
    {
        std::cerr << made.message << '\n';
        return 1;
    }
    EngineStartupPaths startup;
    startup.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    startup.engine_config = PhysicalPath(TOY3D_EDITOR_ENGINE_CONFIG_ROOT);
    startup.saved = saved;
    startup.log_file_name = make_dated_log_file_name("editor");
    if (project->active())
    {
        startup.project_assets = project->assets();
        startup.project_config = project->config();
    }
    if (!g_engine.set_startup_paths(std::move(startup)))
    {
        return 1;
    }
    const auto log_buffer = std::make_shared<LogBuffer>();
    g_engine.initialize_logging(log_buffer);
    if (!project_error.empty())
    {
        TOY_LOG_ERROR("Open Project: {}. Opening the engine default scene.", project_error);
    }
    EditorWorkspace workspace;
    EditorWorkspacePaths paths;
    if (project->active())
    {
        paths.project_assets = project->assets();
    }
    paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
    paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
    paths.saved = saved;
    const bool native_project = project->active() && !project->description().modules.empty();
    if (!workspace.initialize(paths,
                              [&](TypeRegistry& types)
                              {
                                  return !native_project ||
                                         (host.module.register_types && host.module.register_types(types, actor_types));
                              }) ||
        !actor_types.freeze(workspace.types()))
    {
        TOY_LOG_ERROR("Editor workspace initialization: {}", workspace.error());
        g_engine.exit();
        return 1;
    }
    g_engine.set_shader_load_config({ShaderLoadMode::ShaderMapEntry, PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT)});
    g_engine.set_application(
        std::make_unique<EditorApplication>(workspace, log_buffer, project.get(), saved.utf8(), &actor_types,
                                            native_project ? host.game_executable : PhysicalPath{}));
    g_engine.init(hInstance);
    const bool initialized = g_engine.initialized();
    if (initialized)
    {
        g_engine.main_loop();
    }
    g_engine.exit();
    g_engine.set_application(nullptr);
    return initialized ? 0 : 1;
}
