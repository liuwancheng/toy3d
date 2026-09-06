#pragma once

#include "runtime_pch.h"
#include "math/integer_vector.h"
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

    struct Properties
    {
        std::string title = "toy3d";
        Mode mode = Mode::Default;
        Vsync vsync = Vsync::Default;
        Extent extent = {1280, 720};
    };

    class IWindow
    {
      public:
        IWindow() = default;

        virtual ~IWindow() = default;

        virtual bool should_close() = 0;

        virtual void process_events() = 0;

        virtual void close() = 0;

        virtual void resize(uint32_t _width, uint32_t _height)
        {
            properties.extent.width = _width;
            properties.extent.height = _height;
        };
        virtual Extent get_display_size() const { return properties.extent; }
        virtual Extent get_framebuffer_size() const { return properties.extent; }

      public:
        Extent get_win_size() const { return properties.extent; };

        Vsync get_vsync() { return properties.vsync; };

        Mode get_mode() { return properties.mode; };

        IPlatformInput* get_platform_input() const { return platform_input.get(); }

      protected:
        Properties properties;
        std::unique_ptr<IPlatformInput> platform_input;
    };
} // namespace toy3d
