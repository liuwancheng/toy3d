#pragma once
#include "window.h"
#include <memory>

namespace toy3d
{
	class Engine
	{
	public:
		Engine();
		~Engine();

		void pre_init();

		void init();

		void post_init();

		void main_loop();

		void exit();
	private:
		double m_game_time;
		double m_delta_time;
		int m_frame_count;

		std::shared_ptr<IWindow> m_win;
	};
}//toy3d