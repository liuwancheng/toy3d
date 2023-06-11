#include "engine.h"
#include "render/vulkan/mac/mac_window.h"

namespace toy3d
{
	Engine::Engine()
	{
		m_win = std::make_shared<MacWindow>();
	}

	Engine::~Engine()
	{
	}

	void Engine::exit()
	{

	}

	void Engine::pre_init()
	{
		// todo : 一些配置文件的加载
	}

	void Engine::init()
	{
		// todo: 一些其它engine模块的初始化
	}

	void Engine::post_init()
	{
		// todo: game module的初始化
	}

	void Engine::main_loop()
	{
		while (m_win->should_close())
		{
			m_win->process_events();
		}
		m_win->close();
		exit();
	}
	
}