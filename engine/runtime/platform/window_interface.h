#pragma once

#include "runtime_pch.h"
#include "math/integer_vector.h"
#include "platform/platform_input_interface.h"
#include "math/vector2.h"
#include "misc/utf8.h"

#include <cmath>
#include <cstddef>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace toy3d
{
    constexpr std::size_t maximum_file_drop_paths = 32;
    constexpr std::size_t maximum_file_drop_path_bytes = 4096;
    constexpr std::size_t maximum_pending_file_drops = 8;

    struct FileDropEvent
    {
        // Client-area logical coordinates, independent of framebuffer scale.
        Vector2 position;
        std::vector<std::string> paths;
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
        // Deferred close is optional for platform surfaces that can remain live.
        virtual bool cancel_close()
        {
            return false;
        }

        // Native callbacks only enqueue owned events. Applications choose the
        // accepted UI region and consume on the window owner thread.
        virtual bool enable_file_drop(bool enabled)
        {
            return !enabled;
        }
        bool take_file_drop(FileDropEvent& event)
        {
            if (file_drops_.empty())
            {
                return false;
            }
            event = std::move(file_drops_.front());
            file_drops_.pop_front();
            return true;
        }

        virtual void resize(uint32_t _width, uint32_t _height)
        {
            properties.extent.width = _width;
            properties.extent.height = _height;
        };
        virtual Extent get_display_size() const
        {
            return properties.extent;
        }
        virtual Extent get_framebuffer_size() const
        {
            return properties.extent;
        }

      public:
        Extent get_win_size() const
        {
            return properties.extent;
        };

        Vsync get_vsync()
        {
            return properties.vsync;
        };

        Mode get_mode()
        {
            return properties.mode;
        };

        IPlatformInput* get_platform_input() const
        {
            return platform_input.get();
        }

      protected:
        bool enqueue_file_drop(FileDropEvent event)
        {
            if (event.paths.empty() || event.paths.size() > maximum_file_drop_paths ||
                file_drops_.size() >= maximum_pending_file_drops || !std::isfinite(event.position.x) ||
                !std::isfinite(event.position.y))
            {
                return false;
            }
            for (const std::string& path : event.paths)
            {
                if (path.empty() || path.size() > maximum_file_drop_path_bytes ||
                    path.find('\0') != std::string::npos || !is_valid_utf8(path))
                {
                    return false;
                }
            }
            file_drops_.push_back(std::move(event));
            return true;
        }
        void clear_file_drops()
        {
            file_drops_.clear();
        }
        Properties properties;
        std::unique_ptr<IPlatformInput> platform_input;

      private:
        std::deque<FileDropEvent> file_drops_;
    };
} // namespace toy3d
