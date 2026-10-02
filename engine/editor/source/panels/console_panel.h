#pragma once

#include "logging/log_buffer.h"

#include <array>
#include <memory>

namespace toy3d
{
    struct ConsoleLogFilter
    {
        ConsoleLogFilter();
        void defaults();
        void errors_only();
        bool matches(const LogRecord& record) const;
        std::array<bool, log_level_count> levels{};
        std::array<char, 256u> search{};
    };

    class ConsolePanel
    {
      public:
        explicit ConsolePanel(std::shared_ptr<LogBuffer> buffer);
        void draw();
        void open();
        void reveal(std::shared_ptr<const LogRecord> record);
        void open_log_directory();
        void clear_display();
        std::size_t error_count();
        std::size_t warning_count();

      private:
        void refresh();
        std::shared_ptr<LogBuffer> buffer_;
        LogSnapshot snapshot_;
        std::shared_ptr<const LogRecord> revealed_;
        ConsoleLogFilter filter_;
        std::array<std::size_t, log_level_count> counts_{};
        std::uint64_t display_after_ = 0;
        std::uint64_t selected_ = 0;
        std::uint64_t scrolled_sequence_ = 0;
        bool open_ = true;
        bool focus_requested_ = false;
        bool auto_scroll_ = true;
    };
} // namespace toy3d
