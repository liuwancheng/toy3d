#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>

namespace toy3d
{
	struct Extent
	{
		uint32_t width;
		uint32_t height;
	};

	enum class Mode
	{
		Headless,
		Fullscreen,
		FullscreenBorderless,
		FullscreenStretch,
		Default
	};

	enum class Vsync
	{
		OFF,
		ON,
		Default
	};

	struct Properties
	{
		std::string title     = "toy3d";
		Mode        mode      = Mode::Default;
		bool        resizable = true;
		Vsync       vsync     = Vsync::Default;
		Extent      extent    = {1280, 720};
	};

    class IWindow
    {
    public:
        IWindow(uint32_t width, uint32_t height);

        virtual ~IWindow();

        virtual bool should_close() = 0;

        virtual void process_events() = 0;

        virtual void close() = 0;

        virtual void resize(uint32_t width, uint32_t height);
    public:
        Extent get_win_size(){};
    protected:
        Properties properties;
    };
}// namespace toy3d