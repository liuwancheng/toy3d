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
	class ShaderBytecodeProvider;

	class Engine
	{
	public:
		Engine();
		~Engine();

		void pre_init();

		void init(void * hInstance);

		void post_init();

		void main_loop();

		void exit();

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
		std::unique_ptr<ShaderBytecodeProvider> shader_bytecode_provider;
		std::unique_ptr<IPlatform> platform;
		std::unique_ptr<IWindow> window;
		RHISurfaceRef main_window_surface;
		std::unique_ptr<RHIDevice> rhi_device;
		std::unique_ptr<RHIViewportContext> rhi_viewport;
		std::unique_ptr<SceneRendering> scene_renderer;
		std::uint32_t viewport_width = 0;
		std::uint32_t viewport_height = 0;
		bool rhi_initialized = false;
		bool engine_exited = false;
	};
}//toy3d
