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

	Engine::Engine()
	{
	}

	Engine::~Engine()
	{
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

		// 2.创建窗口
	#if WITH_WIN64
		window = std::make_unique<Win32Window>(static_cast<HINSTANCE>(hInstance));
	#elif WITH_MAC
		window = std::make_unique<MacWindow>();
	#elif WITH_ANDROID
		window = std::make_unique<AndroidWindow>();
	#endif
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
		if (!window)
		{
			return;
		}
		while (!window->should_close())
		{
			window->process_events();
		}
	}

	void Engine::exit()
	{
		if (engine_exited)
		{
			return;
		}
		engine_exited = true;
		window.reset();
		platform.reset();
		// todo: resource的释放、文件系统的关闭、游戏模块的关闭等
		Logger::get_instance().exit();
	}

}
