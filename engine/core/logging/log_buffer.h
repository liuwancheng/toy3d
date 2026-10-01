#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace toy3d
{
    enum class LogLevel : std::uint8_t
    {
        TOY_TRACE, TOY_DEBUG, TOY_INFO, TOY_WARN, TOY_ERROR, TOY_CRITICAL, TOY_OFF
    };
    constexpr std::size_t log_level_count = static_cast<std::size_t>(LogLevel::TOY_OFF);

    struct LogRecord
    {
        std::uint64_t sequence = 0;
        std::chrono::system_clock::time_point time{};
        LogLevel level = LogLevel::TOY_INFO;
        std::uint64_t thread_id = 0;
        std::string logger_name;
        std::string source_file;
        int source_line = 0;
        std::string message;
        bool truncated = false;
    };

    struct LogBufferLimits
    {
        std::size_t max_records = 10000u;
        std::size_t max_bytes = 16u * 1024u * 1024u;
    };

    struct LogSnapshot
    {
        // Immutable shared records keep reader snapshots valid after eviction.
        std::vector<std::shared_ptr<const LogRecord>> records;
        std::uint64_t revision = 0;
        std::uint64_t last_sequence = 0;
        std::uint64_t evicted_records = 0;
        std::size_t retained_bytes = 0;
        bool file_requested = false;
        bool file_ready = false;
        std::string file_path;
        std::string output_error;
    };

    class LogBuffer
    {
      public:
        explicit LogBuffer(LogBufferLimits limits = {});
        void append(LogRecord record);
        LogSnapshot snapshot() const;
        std::uint64_t revision() const;
        void configure_file(bool requested, const std::string& path, bool ready);
        void file_flushed();
        // Called directly by sinks, never recursively through Logger.
        void report_output_error(const std::string& message, bool file_failure);

      private:
        void append_locked(LogRecord record);
        LogBufferLimits limits_;
        mutable std::mutex mutex_;
        std::deque<std::shared_ptr<const LogRecord>> records_;
        std::uint64_t revision_ = 0;
        std::uint64_t sequence_ = 0;
        std::uint64_t evicted_records_ = 0;
        std::size_t retained_bytes_ = 0;
        bool file_requested_ = false;
        bool file_ready_ = false;
        std::string file_path_;
        std::string output_error_;
    };
}
