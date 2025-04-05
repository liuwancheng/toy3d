#include "engine.h"
#include "render/vulkan/mac/mac_window.h"

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
	}

	void Engine::init(void* hInstance)
	{
		pre_init();

		// 1.创建平台
	#if WITH_WIN64
		platform = std::make_unique<WinPlatform>(new WinPlatform(hInstance));
	#elif WITH_MAC
		platform = std::make_unique<MacPlatform>(new MacPlatform(hInstance));
	#elif WITH_ANDROID
		platform = std::make_unique<AndroidPlatform>(new AndroidPlatform(hInstance));
	#endif

		// 2.创建窗口
	#if WITH_WIN64
		window = std::make_unique<Win32Window>();
	#elif WITH_MAC
		window = std::make_unique<MacWindow>();
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
		while (window->should_close())
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