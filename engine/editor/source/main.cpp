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

toy3d::Engine g_engine;

#if WITH_WIN
std::string wchar2string(const wchar_t* text)
{
    if (!text) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), bytes, nullptr, nullptr)) return {};
    result.pop_back();
    return result;
}
#endif

// 引擎主函数声明
int engine_main(void* hInstance);

#if WITH_WIN

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    if (ctrlType == CTRL_CLOSE_EVENT)
    {
        g_engine.exit();
        Sleep(500);
        return TRUE;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    // attempt to attach to the parent process console if it exists
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        // No parent console, allocate a new one for this process
        if (!AllocConsole())
        {
            return -1;
        }
    }

    FILE* fp;
    freopen_s(&fp, "conin$", "r", stdin);
    freopen_s(&fp, "conout$", "w", stdout);
    freopen_s(&fp, "conout$", "w", stderr);

    // 注册处理函数
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // process command line args
    int argc = 0;
    LPWSTR* argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvw) return 1;
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++)
    {
        const std::string argument = wchar2string(argvw[i]);
        if (argument.empty() && argvw[i][0] != L'\0') { LocalFree(argvw); return 1; }
        args.push_back(argument);
    }
    LocalFree(argvw);
    toy3d::CommandLineParser::get_instance().parser_args(args);

    // start engine
    return engine_main(static_cast<void*>(hInstance));
}
#else
int main(int argc, char* argv[])
{
    // process command line args
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++)
    {
        args.push_back(argv[i]);
    }
    toy3d::CommandLineParser::get_instance().parser_args(args);

    // start engine
    return engine_main(nullptr);
}
#endif

int engine_main(void* hInstance)
{
    using namespace toy3d;
    auto& arguments = CommandLineParser::get_instance();
    const PhysicalPath editor_directory(TOY3D_EDITOR_DEPLOY_ROOT);
    auto project = std::make_unique<EditorProject>(editor_directory);
    std::string project_error;
    if (arguments.has_option("Project"))
    {
        const auto opened = project->open(PhysicalPath(arguments.get_option("Project")));
        if (!opened.succeeded()) { project_error = opened.message; project = std::make_unique<EditorProject>(editor_directory); }
    }
    NativePlatformFile platform;
    PhysicalPath saved;
    if (project->active()) saved = project->saved();
    else
    {
        const auto user = user_data_directory();
        if (!user.succeeded()) { std::cerr << user.status().message << '\n'; return 1; }
        const auto path = platform.join_relative(user.value(), "Toy3d/Editor");
        if (!path.succeeded()) { std::cerr << path.status().message << '\n'; return 1; }
        saved = path.value();
    }
    const auto made = platform.create_directories(saved);
    if (!made.succeeded()) { std::cerr << made.message << '\n'; return 1; }
    AssetId session;
    if (!AssetId::try_generate(session)) return 1;
    EngineStartupPaths startup;
    startup.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    startup.engine_config = PhysicalPath(TOY3D_EDITOR_ENGINE_CONFIG_ROOT);
    startup.saved = saved; startup.log_file_name = "editor-" + session.hex() + ".log";
    if (project->active()) { startup.project_assets = project->assets(); startup.project_config = project->config(); }
    if (!g_engine.set_startup_paths(std::move(startup))) return 1;
    const auto log_buffer = std::make_shared<LogBuffer>();
    g_engine.initialize_logging(log_buffer);
    if (!project_error.empty()) TOY_LOG_ERROR("Open Project: {}. Opening the engine default scene.", project_error);
    EditorWorkspace workspace;
    EditorWorkspacePaths paths;
    if (project->active()) paths.project_assets = project->assets();
    paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
    paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
    paths.saved = saved;
    if (!workspace.initialize(paths))
    { TOY_LOG_ERROR("Editor workspace initialization: {}", workspace.error()); g_engine.exit(); return 1; }
    g_engine.set_shader_load_config({ShaderLoadMode::ShaderMapEntry, PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT)});
    g_engine.set_application(std::make_unique<EditorApplication>(workspace, log_buffer, project.get(), saved.utf8()));
    g_engine.init(hInstance);
    const bool initialized = g_engine.initialized();
    if (initialized) g_engine.main_loop();
    g_engine.exit(); g_engine.set_application(nullptr);
    return initialized ? 0 : 1;
}
