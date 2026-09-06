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
    class ShaderMapProgram;
    struct ImGuiDrawData;

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

    class Engine
    {
      public:
        Engine();
        ~Engine();

        void pre_init();

        void init(void* hInstance);

        void main_loop();

        void exit();

        void set_shader_load_config(ShaderLoadConfig config);
        void set_application(std::unique_ptr<Application> value);

        IWindow* get_window() { return window.get(); }

      private:
        FileStatus initialize_file_system();
        bool initialize_builtin_shader_programs();
        bool initialize_render_framework();
        void shutdown_render_framework();
        void submit_frame_draw(std::unique_ptr<ImGuiDrawData> ui_draw_data);

        double game_time = 0.0;
        double delta_time = 0.0;
        int frame_count = 0;

        NativePlatformFile native_platform_file;
        std::shared_ptr<DirectoryFileStore> engine_asset_store;
        std::shared_ptr<DirectoryFileStore> engine_shader_store;
        std::shared_ptr<DirectoryFileStore> saved_store;
        std::shared_ptr<DirectoryFileStore> temp_store;
        FileSystem file_system;
        ShaderLoadConfig shader_load_config;
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
