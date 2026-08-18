#pragma once

#include "runtime_pch.h"
#include "platform/platform_input_interface.h"

namespace toy3d
{
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

	struct Extent
	{
		uint32_t width;
		uint32_t height;
	};

	struct Properties
	{
		std::string title     = "toy3d";
		Mode        mode      = Mode::Default;
		Vsync       vsync     = Vsync::Default;
		Extent      extent    = {1280, 720};
	};

    class IWindow
    {
    public:
        IWindow(){};

        virtual ~IWindow(){};

        virtual bool should_close() = 0;

        virtual void process_events() = 0;

        virtual void close() = 0;

        virtual void resize(uint32_t _width, uint32_t _height)
		{
			properties.extent.width = _width;
			properties.extent.height = _height;
		};
    public:
        Extent get_win_size(){return properties.extent;};

        Vsync get_vsync(){return properties.vsync;};

        Mode get_mode(){return properties.mode;};

		IPlatformInput* get_platform_input() const { return platform_input.get();}
    protected:
        Properties properties;
		std::unique_ptr<IPlatformInput> platform_input;
    };
}// namespace toy3d
