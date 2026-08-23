#include "engine.h"

#include "config/command_line_parser.h"
#include "config/console_manager.h"

#if WITH_WIN64
#include "platform/win/win32_platform.h"
#include "platform/win/win32_window.h"
#elif WITH_MAC
#include "platform/mac/mac_platform.h"
#include "platform/mac/mac_window.h"
#elif WITH_ANDROID
#include "platform/android/android_platform.h"
#include "platform/android/android_window.h"
#endif

#include "logging/logger.h"
#include "generated/defines.h"
#include "platform/rhi_surface_factory.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/rendering_thread.h"
#include "renderscene/renderer.h"
#include "task_graph/task_graph.h"
#include "threading/thread_manager.h"

#include <filesystem>
#include <iostream>
#include <utility>

namespace toy3d
{
	namespace
	{
		FileStatus add_directory_mount(
			FileSystem& file_system,
			const char* virtual_root,
			const std::shared_ptr<DirectoryFileStore>& store,
			MountAccess access,
			bool allow_enumeration,
			const char* debug_name)
		{
			auto parsed_root = VirtualPath::parse(virtual_root);
			if (!parsed_root.succeeded())
			{
				return parsed_root.status();
			}
			FileMountDesc descriptor;
			descriptor.virtual_root = parsed_root.value();
			descriptor.store = store;
			descriptor.access = access;
			descriptor.allow_enumeration = allow_enumeration;
			descriptor.debug_name = debug_name;
			return file_system.add_mount(descriptor);
		}

	}

    Engine::Engine() = default;

    Engine::~Engine()
    {
        exit();
    }

	void Engine::set_shader_load_config(ShaderLoadConfig config)
	{
		if (!window)
		{
			shader_load_config = std::move(config);
		}
	}

	void Engine::pre_init()
	{
		// 初始化日志系统
		LogConfig log_config;
		log_config.logger_name = "Toy3dRuntime";
		// filesystem composes the platform-native saved/log path without manual
		// separator handling at the runtime composition root.
		log_config.log_directory = std::filesystem::path(ENGINE_SAVED_ROOT) / "logs";
		log_config.file_name = "toy3d.log";
		std::string log_error;
		if (!Logger::get_instance().init(log_config, &log_error))
		{
			std::cerr << "Failed to initialize Toy3d logging: " << log_error << '\n';
		}

		// 1. Initialize the shared file system.
		const FileStatus file_system_status = initialize_file_system();
		if (!file_system_status.succeeded())
		{
			TOY_LOG_ERROR(
				"Runtime file system initialization failed during {}: {}",
				file_system_status.operation,
				file_system_status.message);
			return;
		}
		// 2. Load engine configuration.
		auto config_path = VirtualPath::parse("/Engine/config/engine_config.ini");
		if (!config_path.succeeded())
		{
			TOY_LOG_ERROR("The built-in engine config path is invalid.");
			return;
		}
		const FileStatus config_status = ConsoleManager::get_instance().load_config(
			file_system,
			config_path.value());
		if (!config_status.succeeded())
		{
			TOY_LOG_ERROR(
				"Failed to load {}: {}",
				config_path.value().utf8(),
				config_status.message);
		}
		// 3. Apply command-line configuration overrides.
		CommandLineParser::get_instance().apply_config();
	}

	void Engine::init(void* hInstance)
	{
		pre_init();
		// 1.创建平台
	#if WITH_WIN64
		platform = std::make_unique<Win32Platform>();
	#elif WITH_MAC
		platform = std::make_unique<MacPlatform>();
	#elif WITH_ANDROID
		platform = std::make_unique<AndroidPlatform>();
	#endif

        if (!platform || !platform->init())
        {
            TOY_LOG_ERROR("Runtime platform initialization failed.");
            exit();
            return;
        }
        platform_initialized = true;

		// 2.创建窗口
	#if WITH_WIN64
		window = std::make_unique<Win32Window>(static_cast<HINSTANCE>(hInstance));
	#elif WITH_MAC
		window = std::make_unique<MacWindow>();
	#elif WITH_ANDROID
		window = std::make_unique<AndroidWindow>();
	#endif

        if (!window)
        {
            TOY_LOG_ERROR("Runtime window creation failed.");
            exit();
            return;
        }

        RHIResult<RHISurfaceRef> created_surface = create_rhi_surface(*window);
        if (!created_surface.succeeded())
        {
            TOY_LOG_ERROR(
                "Runtime RHI surface creation failed: {}",
                created_surface.status().message());
            exit();
            return;
        }
        rhi_surface = std::move(created_surface).value();

        if (!initialize_render_framework())
        {
            exit();
        }
	}

    bool Engine::initialize_render_framework()
    {
        thread_manager = std::make_unique<ThreadManager>();

        const bool use_rendering_thread = ConsoleManager::get_instance().get_bool(
            "Renderer.MultiThreaded", true);
        TaskGraphConfig task_graph_config;
        task_graph_config.multithreaded = use_rendering_thread;
        TaskGraphCreateResult created_task_graph = create_task_graph(
            task_graph_config, *thread_manager);
        if (!created_task_graph.succeeded())
        {
            TOY_LOG_ERROR(
                "Runtime Task Graph creation failed: {}",
                created_task_graph.status().message);
            shutdown_render_framework();
            return false;
        }
        task_graph = created_task_graph.take_task_graph();

        const TaskGraphStatus attached =
            task_graph->attach_to_thread(NamedThread::GameThread);
        if (!attached.succeeded())
        {
            TOY_LOG_ERROR("GameThread attach failed: {}", attached.message);
            shutdown_render_framework();
            return false;
        }

        renderer = std::make_unique<Renderer>(*task_graph);
        rendering_thread = std::make_unique<RenderingThread>(
            *thread_manager,
            *task_graph,
            use_rendering_thread
                ? RenderingThreadMode::MultiThread
                : RenderingThreadMode::SingleThread);
        const ThreadStatus started = rendering_thread->start(
            [this]()
            {
                return renderer->initialize();
            });
        if (!started.succeeded())
        {
            TOY_LOG_ERROR("RenderingThread startup failed: {}", started.message);
            shutdown_render_framework();
            return false;
        }

        frame_end_sync = std::make_unique<FrameEndSync>(
            ConsoleManager::get_instance().get_bool(
                "Renderer.AllowOneFrameThreadLag", true));
        return true;
    }

    void Engine::shutdown_render_framework()
    {
        frame_end_sync.reset();

        if (rendering_thread)
        {
            if (rendering_thread->is_ready())
            {
                const RenderFenceWaitResult drained = flush_rendering_commands();
                if (!drained.succeeded())
                {
                    TOY_LOG_ERROR(
                        "Rendering command drain failed during shutdown: {}",
                        drained.framework_status().message);
                }
            }

            const ThreadStatus stopped = rendering_thread->stop(
                [this]()
                {
                    return renderer != nullptr
                        ? renderer->teardown()
                        : ThreadStatus::success();
                });
            if (!stopped.succeeded())
            {
                TOY_LOG_ERROR("RenderingThread shutdown failed: {}", stopped.message);
            }
            rendering_thread.reset();
        }

        renderer.reset();

        if (task_graph)
        {
            const TaskGraphShutdownResult stopped = task_graph->shutdown(
                TaskGraphShutdownMode::Drain);
            if (!stopped.succeeded())
            {
                TOY_LOG_ERROR("Task Graph shutdown failed: {}", stopped.status.message);
            }
            task_graph.reset();
        }
        thread_manager.reset();
    }

	FileStatus Engine::initialize_file_system()
	{
		if (file_system.frozen())
		{
			return FileStatus::success();
		}
		const PhysicalPath deployment_root(ENGINE_ASSET_ROOT);
		const PhysicalPath saved_root(ENGINE_SAVED_ROOT);
		auto shader_root = native_platform_file.join_relative(deployment_root, "shader");
		if (!shader_root.succeeded())
		{
			return shader_root.status();
		}
		auto asset_root = native_platform_file.join_relative(deployment_root, "asset");
		if (!asset_root.succeeded())
		{
			return asset_root.status();
		}
		auto temp_root = native_platform_file.join_relative(saved_root, "temp");
		if (!temp_root.succeeded())
		{
			return temp_root.status();
		}

		FileStatus status = native_platform_file.create_directories(saved_root);
		if (!status.succeeded())
		{
			return status;
		}
		status = native_platform_file.create_directories(temp_root.value());
		if (!status.succeeded())
		{
			return status;
		}

		auto create_store = [this](
			const PhysicalPath& root,
			bool writable,
			const char* debug_name)
		{
			DirectoryFileStoreDesc descriptor;
			descriptor.physical_root = root;
			descriptor.writable = writable;
			descriptor.symlink_policy = DirectorySymlinkPolicy::Deny;
			descriptor.debug_name = debug_name;
			return DirectoryFileStore::create(native_platform_file, descriptor);
		};

		auto engine_assets = create_store(asset_root.value(), false, "RuntimeEngineAssets");
		if (!engine_assets.succeeded())
		{
			return engine_assets.status();
		}
		engine_asset_store = engine_assets.value();
		auto engine_shaders = create_store(shader_root.value(), false, "RuntimeEngineShaders");
		if (!engine_shaders.succeeded())
		{
			return engine_shaders.status();
		}
		engine_shader_store = engine_shaders.value();
		auto saved = create_store(saved_root, true, "RuntimeSaved");
		if (!saved.succeeded())
		{
			return saved.status();
		}
		saved_store = saved.value();
		auto temp = create_store(temp_root.value(), true, "RuntimeTemp");
		if (!temp.succeeded())
		{
			return temp.status();
		}
		temp_store = temp.value();

		status = add_directory_mount(
			file_system, "/Engine", engine_asset_store, MountAccess::ReadOnly, true, "Engine");
		if (!status.succeeded()) return status;
		status = add_directory_mount(
			file_system, "/Engine/Shader", engine_shader_store, MountAccess::ReadOnly, true, "EngineShader");
		if (!status.succeeded()) return status;
		status = add_directory_mount(
			file_system, "/Project", engine_asset_store, MountAccess::ReadOnly, true, "Project");
		if (!status.succeeded()) return status;
		status = add_directory_mount(
			file_system, "/Saved", saved_store, MountAccess::ReadWrite, true, "Saved");
		if (!status.succeeded()) return status;
		status = add_directory_mount(
			file_system, "/Temp", temp_store, MountAccess::ReadWrite, true, "Temp");
		if (!status.succeeded()) return status;
		status = file_system.freeze();
		if (!status.succeeded()) return status;

		return FileStatus::success();
	}

	void Engine::main_loop()
	{
		if (!window || !frame_end_sync)
		{
			return;
		}
		while (!window->should_close())
		{
			window->process_events();
            const RenderFenceWaitResult synchronized = frame_end_sync->sync_frame();
            if (!synchronized.succeeded())
            {
                TOY_LOG_ERROR(
                    "Frame synchronization failed: {}",
                    synchronized.framework_status().message);
                break;
            }
		}
	}

	void Engine::exit()
	{
		if (engine_exited)
		{
			return;
		}
		engine_exited = true;
		shutdown_render_framework();
		rhi_surface.reset();
		window.reset();
		if (platform_initialized && platform)
		{
			platform->exit();
			platform_initialized = false;
		}
		platform.reset();
		// todo: resource的释放、文件系统的关闭、游戏模块的关闭等
		Logger::get_instance().exit();
	}

}
