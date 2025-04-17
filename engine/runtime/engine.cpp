#include "engine.h"
#include "core/config/config_manager.h"
#include "core/config/command_line_parser.h"

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

#include "core/file_system/file_system.h"

namespace toy3d
{
	Engine::Engine()
	{
	}

	Engine::~Engine()
	{
	}

	void Engine::pre_init()
	{
		// 1.配置文件的加载
		ConfigManager::get_instance().load_config_file("engine_config.ini");
		// 2.命令行参数override 配置文件的参数
		CommandLineParser::get_instance().apply_config();
		// 3.初始化文件系统
		FileSystem::get_instance().initialize();
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

		post_init();
		// 3.创建RHI
	}

	void Engine::post_init()
	{
		// todo: game module的初始化
	}

	void Engine::main_loop()
	{
		while (!window->should_close())
		{
			window->process_events();
		}
		window->close();
		exit();
	}

	void Engine::exit()
	{
		// todo: resource的释放、文件系统的关闭、游戏模块的关闭等
	}
	
}