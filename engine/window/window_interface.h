#pragma once

#include "pch.h"

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

	class IPlatformInput;

    class IWindow
    {
    public:
        IWindow(){};

        virtual ~IWindow(){};

        virtual bool should_close() = 0;

        virtual void process_events() = 0;

        virtual void close() = 0;

        virtual void resize(uint32_t width, uint32_t height){};
    public:
        Extent get_win_size(){return properties_.extent;};

        Vsync get_vsync(){return properties_.vsync;};

        Mode get_mode(){return properties_.mode;};
    protected:
        Properties properties_;
		std::unique_ptr<IPlatformInput> platform_input_;
    };
}// namespace toy3d