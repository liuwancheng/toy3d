#pragma once
#include "pch.h"

namespace toy3d
{
	class IPlatform;
	class IWindow;
	class RHIInterface;

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
		std::unique_ptr<RHIInterface> rhi;
	};
}//toy3d