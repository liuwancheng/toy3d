#pragma once

#include "runtime_pch.h"
#include "file_system/directory_file_store.h"
#include "file_system/file_system.h"
#include "file_system/native_platform_file.h"
#include "drivers/rhi/rhi.h"
#include "platform/platform_interface.h"
#include "platform/window_interface.h"

namespace toy3d
{
	class IPlatform;
	class IWindow;
	class SceneRendering;
	class ShaderMapLoader;
	class ShaderMap;
	class RenderResourceCache;

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

		void init(void * hInstance);

		RHIStatus post_init();

		void main_loop();

		void exit();

		void set_shader_load_config(ShaderLoadConfig config);

		IWindow* get_window() { return window.get(); };
	private:
		FileStatus initialize_file_system();
		RHIStatus initialize_rhi();
		void shutdown_rhi();
		void render_frame();
		void log_rhi_failure(const char* operation, const RHIStatus& status) const;

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
		std::unique_ptr<ShaderMapLoader> shader_map_loader;
		std::unique_ptr<ShaderMap> shader_map;
		std::unique_ptr<IPlatform> platform;
		std::unique_ptr<IWindow> window;
		RHISurfaceRef main_window_surface;
		std::unique_ptr<RHIDevice> rhi_device;
		std::unique_ptr<RHIViewportContext> rhi_viewport;
		std::unique_ptr<RenderResourceCache> render_resource_cache;
		std::unique_ptr<SceneRendering> scene_renderer;
		std::uint32_t viewport_width = 0;
		std::uint32_t viewport_height = 0;
		bool rhi_initialized = false;
		bool engine_exited = false;
	};
}//toy3d
