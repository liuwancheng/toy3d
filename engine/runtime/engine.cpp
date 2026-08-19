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
#include "drivers/rhi/rhi_factory.h"
#include "generated/defines.h"
#include "platform/rhi_surface_factory.h"
#include "renderscene/3dscene/forward_shading_render.h"
#include "renderscene/resources/render_resource_cache.h"
#include "rendercore/shader/loaders/shader_code_library_loader.h"
#if TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#endif
#include "rendercore/shader/shader_map.h"

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
		if (!rhi_initialized)
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
		switch (shader_load_config.mode)
		{
		case ShaderLoadMode::ShaderMapEntry:
#if TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING
			shader_map_loader = std::make_unique<ShaderMapEntryLoader>(shader_load_config.path);
#else
			TOY_LOG_ERROR("ShaderMapEntry loading is not enabled in this build.");
			return;
#endif
			break;
		case ShaderLoadMode::ShaderCodeLibrary:
			shader_map_loader = std::make_unique<ShaderCodeLibraryLoader>(shader_load_config.path);
			break;
		}
		if (!shader_map_loader)
		{
			TOY_LOG_ERROR("Engine initialization requires a ShaderMapLoader.");
			return;
		}

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

		const RHIStatus rhi_status = initialize_rhi();
		if (!rhi_status)
		{
			log_rhi_failure("initialize_rhi", rhi_status);
			window->close();
			return;
		}
		const RHIStatus renderer_status = post_init();
		if (!renderer_status)
		{
			log_rhi_failure("post_init", renderer_status);
			shutdown_rhi();
			window->close();
			return;
		}
		// 3.创建RHI
	}

	RHIStatus Engine::post_init()
	{
		shader_map = std::make_unique<ShaderMap>(*shader_map_loader);
		auto cache = std::make_unique<RenderResourceCache>(
			create_builtin_render_resource_placeholders());
		const RHIStatus bootstrap_status =
			cache->initialize_rhi_placeholders(*rhi_device);
		if (!bootstrap_status)
		{
			return bootstrap_status;
		}
		render_resource_cache = std::move(cache);
		scene_renderer = std::make_unique<ForwardSceneRendering>(
			*rhi_device,
			*shader_map);
		// todo: game module的初始化
		return RHIStatus::success();
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
			render_frame();
		}
	}

	void Engine::exit()
	{
		if (engine_exited)
		{
			return;
		}
		engine_exited = true;
		shutdown_rhi();
		window.reset();
		platform.reset();
		// todo: resource的释放、文件系统的关闭、游戏模块的关闭等
		Logger::get_instance().exit();
	}

	RHIStatus Engine::initialize_rhi()
	{
		if (rhi_initialized)
		{
			return RHIStatus::success();
		}
		if (!window)
		{
			return RHIStatus::failure(RHIErrorCode::NotReady, "RHI initialization requires a window.");
		}

		auto surface_result = create_rhi_surface(*window);
		if (!surface_result)
		{
			return surface_result.status();
		}
		main_window_surface = std::move(surface_result).value();

		auto device_result = create_default_rhi_device();
		if (!device_result)
		{
			main_window_surface.reset();
			return device_result.status();
		}
		rhi_device = std::move(device_result).value();
		RHIDeviceDesc device_desc;
		device_desc.primary_surface = main_window_surface;
		device_desc.enable_validation = true;
		device_desc.debug_name = "Toy3dMainDevice";
		RHIStatus status = rhi_device->initialize(device_desc);
		if (!status)
		{
			rhi_device.reset();
			main_window_surface.reset();
			return status;
		}

		const Extent extent = window->get_win_size();
		if (extent.width == 0 || extent.height == 0)
		{
			shutdown_rhi();
			return RHIStatus::failure(RHIErrorCode::NotReady, "RHI viewport requires a non-zero window extent.");
		}
		RHIViewportContextDesc viewport_desc;
		viewport_desc.width = extent.width;
		viewport_desc.height = extent.height;
		viewport_desc.image_count = 2;
		viewport_desc.format = RHIFormat::B8G8R8A8UNorm;
		viewport_desc.present_mode = RHIPresentMode::Fifo;
		viewport_desc.debug_name = "Toy3dMainViewport";
		auto viewport_result = rhi_device->create_viewport_context(main_window_surface, viewport_desc);
		if (!viewport_result)
		{
			status = viewport_result.status();
			shutdown_rhi();
			return status;
		}
		rhi_viewport = std::move(viewport_result).value();
		viewport_width = extent.width;
		viewport_height = extent.height;
		rhi_initialized = true;
		TOY_LOG_INFO("The default RHI is connected to the main window at {}x{}.", viewport_width, viewport_height);
		return RHIStatus::success();
	}

	void Engine::shutdown_rhi()
	{
		if (!rhi_device)
		{
			return;
		}
		scene_renderer.reset();
		render_resource_cache.reset();
		shader_map.reset();
		shader_map_loader.reset();
		rhi_viewport.reset();
		const RHIStatus status = rhi_device->shutdown();
		if (!status)
		{
			log_rhi_failure("RHIDevice::shutdown", status);
		}
		rhi_device.reset();
		main_window_surface.reset();
		rhi_initialized = false;
	}

	void Engine::render_frame()
	{
		if (!rhi_initialized || !rhi_viewport || !window)
		{
			return;
		}
		const Extent extent = window->get_win_size();
		if (extent.width == 0 || extent.height == 0)
		{
			return;
		}
		if (extent.width != viewport_width || extent.height != viewport_height)
		{
			const RHIStatus resize_status = rhi_viewport->request_resize(extent.width, extent.height);
			if (!resize_status)
			{
				log_rhi_failure("RHIViewportContext::request_resize", resize_status);
				return;
			}
			viewport_width = extent.width;
			viewport_height = extent.height;
		}

		auto frame_result = rhi_viewport->begin_frame();
		if (!frame_result)
		{
			if (!rhi_is_recoverable_viewport_status(frame_result.status()))
			{
				log_rhi_failure("RHIViewportContext::begin_frame", frame_result.status());
			}
			return;
		}
		std::unique_ptr<RHIFrameContext> frame = std::move(frame_result).value();
		if (!scene_renderer)
		{
			TOY_LOG_ERROR("A viewport frame was acquired without a scene renderer.");
			const RHIStatus abort_status = rhi_viewport->abort_frame(std::move(frame));
			if (!abort_status && !rhi_is_recoverable_viewport_status(abort_status))
			{
				log_rhi_failure("RHIViewportContext::abort_frame", abort_status);
			}
			return;
		}
		auto command_list_result = scene_renderer->render(*frame);
		if (!command_list_result)
		{
			log_rhi_failure("SceneRendering::render", command_list_result.status());
			const RHIStatus abort_status = rhi_viewport->abort_frame(std::move(frame));
			if (!abort_status && !rhi_is_recoverable_viewport_status(abort_status))
			{
				log_rhi_failure("RHIViewportContext::abort_frame", abort_status);
			}
			return;
		}
		std::vector<RHICommandListRef> command_lists;
		command_lists.push_back(std::move(command_list_result).value());
		const RHIStatus status = rhi_viewport->end_frame(std::move(frame), command_lists);
		if (!status && !rhi_is_recoverable_viewport_status(status))
		{
			log_rhi_failure("RHIViewportContext::end_frame", status);
		}
	}

	void Engine::log_rhi_failure(const char* operation, const RHIStatus& status) const
	{
		TOY_LOG_ERROR("{} failed: {}", operation, status.message());
	}
}
