#pragma once
#include "pch.h"

namespace toy3d
{
	class PlatformInterface;
	class WindowInterface;
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

		std::unique_ptr<PlatformInterface> platform;
		std::unique_ptr<WindowInterface> window;
		std::unique_ptr<RHIInterface> rhi;
	};
}//toy3d