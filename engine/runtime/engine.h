#pragma once

#include "runtime_pch.h"
#include "file_system/directory_file_store.h"
#include "file_system/file_system.h"
#include "file_system/native_platform_file.h"
#include "platform/platform_interface.h"
#include "platform/window_interface.h"
#include "rendercore/view/scene_view.h"

#include <memory>

namespace toy3d
{
    class Application;
    class LogBuffer;
    class FrameEndSync;
    class IPlatform;
    class IWindow;
    class Renderer;
    class RenderingThread;
    class RHISurface;
    class TaskGraphInterface;
    class ThreadManager;
    class World;
    class ImGuiSystem;
    class GlobalShaderMap;
    class ShaderMap;
    class ShaderMapLoader;
    struct BuiltinMeshPassPrograms;
    struct ImGuiDrawData;
    struct ViewportFrameOutput;

    enum class ShaderLoadMode
    {
        ShaderMapEntry,
        ShaderCodeLibrary
    };

    struct ShaderLoadConfig
    {
        ShaderLoadMode mode = ShaderLoadMode::ShaderCodeLibrary;
        PhysicalPath path;
    };

    struct EngineStartupPaths
    {
        PhysicalPath engine_assets;
        PhysicalPath engine_config;
        PhysicalPath project_assets;
        PhysicalPath project_config;
        PhysicalPath saved;
        std::string log_file_name = "toy3d.log";
    };

    class Engine
    {
      public:
        Engine();
        ~Engine();

        bool pre_init();
        bool set_startup_paths(EngineStartupPaths paths);
        // Call before pre_init() to capture startup diagnostics. Subsequent calls
        // reuse the session outputs; they do not attach a new buffer or reconfigure.
        bool initialize_logging(std::shared_ptr<LogBuffer> buffer = {});

        void init(void* hInstance);

        bool initialized() const { return world && renderer && window && !engine_exited; }
        void main_loop();

        void exit();

        void set_shader_load_config(ShaderLoadConfig config);
        void set_application(std::unique_ptr<Application> value);

        IWindow* get_window() { return window.get(); }

      private:
        FileStatus initialize_file_system();
        bool initialize_builtin_shader_programs(BuiltinMeshPassPrograms& mesh_pass_programs);
        bool initialize_render_framework(BuiltinMeshPassPrograms mesh_pass_programs);
        void shutdown_render_framework();
        void submit_frame_draw(std::unique_ptr<ImGuiDrawData> ui_draw_data, ViewportFrameOutput output);

        double game_time = 0.0;
        double delta_time = 0.0;
        int frame_count = 0;

        NativePlatformFile native_platform_file;
        bool logging_started_ = false;
        bool logging_outputs_ready_ = false;
        std::shared_ptr<DirectoryFileStore> engine_asset_store;
        std::shared_ptr<DirectoryFileStore> engine_shader_store;
        std::shared_ptr<DirectoryFileStore> saved_store;
        std::shared_ptr<DirectoryFileStore> temp_store;
        FileSystem file_system;
        ShaderLoadConfig shader_load_config;
        EngineStartupPaths startup_paths;
        std::unique_ptr<ShaderMapLoader> builtin_shader_loader;
        std::unique_ptr<ShaderMap> builtin_shader_map;
        std::shared_ptr<const GlobalShaderMap> global_shader_map;
        std::unique_ptr<Application> application;
        std::unique_ptr<IPlatform> platform;
        std::unique_ptr<IWindow> window;
        std::shared_ptr<RHISurface> rhi_surface;
        std::unique_ptr<ThreadManager> thread_manager;
        std::unique_ptr<TaskGraphInterface> task_graph;
        std::unique_ptr<Renderer> renderer;
        std::unique_ptr<RenderingThread> rendering_thread;
        std::unique_ptr<FrameEndSync> frame_end_sync;
        std::unique_ptr<World> world;
        std::unique_ptr<ImGuiSystem> imgui_system;
        bool application_bound = false;
        bool platform_initialized = false;
        bool engine_exited = false;
    };
} // namespace toy3d
