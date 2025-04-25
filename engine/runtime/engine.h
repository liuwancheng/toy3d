#pragma once

#include "core/misc/pch.h"
#include "platform/platform_interface.h"
#include "platform/window_interface.h"

namespace toy3d
{
	class IPlatform;
	class IWindow;

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
		double game_time = 0.0;
		double delta_time = 0.0;
		int frame_count = 0;

		std::unique_ptr<IPlatform> platform;
		std::unique_ptr<IWindow> window;
	};
}//toy3d