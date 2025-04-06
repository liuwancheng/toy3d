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
	private:
		double game_time;
		double delta_time;
		int frame_count;

		std::unique_ptr<IPlatform> platform;
		std::unique_ptr<IWindow> window;
	};
}//toy3d