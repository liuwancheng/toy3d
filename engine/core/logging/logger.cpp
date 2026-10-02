#include "logging/logger.h"

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <utility>
#include <vector>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/os.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace toy3d
{
    namespace
    {
        LogLevel log_level(spdlog::level::level_enum level)
        {
            switch (level)
            {
            case spdlog::level::trace:
                return LogLevel::TOY_TRACE;
            case spdlog::level::debug:
                return LogLevel::TOY_DEBUG;
            case spdlog::level::info:
                return LogLevel::TOY_INFO;
            case spdlog::level::warn:
                return LogLevel::TOY_WARN;
            case spdlog::level::err:
                return LogLevel::TOY_ERROR;
            case spdlog::level::critical:
                return LogLevel::TOY_CRITICAL;
            default:
                return LogLevel::TOY_OFF;
            }
        }

        void report_output_error(const std::shared_ptr<LogBuffer>& buffer, const std::string& message,
                                 bool file_failure) noexcept
        {
            std::fprintf(stderr, "Toy3d logging output failure: %s\n", message.c_str());
            if (buffer)
            {
                try
                {
                    buffer->report_output_error(message, file_failure);
                }
                catch (...)
                {
                    std::fputs("Unable to retain logging failure in memory.\n", stderr);
                }
            }
        }

        // --------------------------------------------------------------------------
        // MemoryLogSink: copy producer-owned spdlog data into the bounded session buffer
        // --------------------------------------------------------------------------
        class MemoryLogSink final : public spdlog::sinks::base_sink<std::mutex>
        {
          public:
            explicit MemoryLogSink(std::shared_ptr<LogBuffer> buffer) : buffer_(std::move(buffer))
            {
            }

          private:
            void sink_it_(const spdlog::details::log_msg& message) override
            {
                LogRecord record;
                record.time = message.time;
                record.level = log_level(message.level);
                record.thread_id = message.thread_id;
                record.logger_name.assign(message.logger_name.data(), message.logger_name.size());
                if (message.source.filename)
                {
                    record.source_file = message.source.filename;
                }
                record.source_line = message.source.line;
                record.message.assign(message.payload.data(), message.payload.size());
                buffer_->append(std::move(record));
            }
            void flush_() override
            {
            }
            std::shared_ptr<LogBuffer> buffer_;
        };

        // --------------------------------------------------------------------------
        // FileLogSink: preserve independent outputs when rotating-file I/O fails
        // --------------------------------------------------------------------------
        class FileLogSink final : public spdlog::sinks::sink
        {
          public:
            FileLogSink(const std::filesystem::path& path, const LogConfig& config)
                : file_(path.native(), config.max_file_size, config.max_file_count, false),
                  buffer_(config.memory_output)
            {
                file_.set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [thread %t] [%s:%#] %v");
            }
            void log(const spdlog::details::log_msg& message) override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                try
                {
                    file_.log(message);
                    write_failed_ = false;
                }
                catch (const std::exception& error)
                {
                    write_failed_ = true;
                    report_output_error(buffer_, error.what(), true);
                }
            }
            void flush() override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                try
                {
                    file_.flush();
                    if (!write_failed_ && buffer_)
                    {
                        buffer_->file_flushed();
                    }
                }
                catch (const std::exception& error)
                {
                    write_failed_ = true;
                    report_output_error(buffer_, error.what(), true);
                }
            }
            void set_pattern(const std::string& pattern) override
            {
                file_.set_pattern(pattern);
            }
            void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override
            {
                file_.set_formatter(std::move(formatter));
            }

          private:
            spdlog::sinks::rotating_file_sink_mt file_;
            std::shared_ptr<LogBuffer> buffer_;
            std::mutex mutex_;
            bool write_failed_ = false;
        };
    } // namespace

    std::string make_dated_log_file_name(const std::string& role)
    {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        // Reuse spdlog's thread-safe native time/PID adapters across platforms.
        const auto local = spdlog::details::os::localtime(time);
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        return spdlog::fmt_lib::format("{}-{:04}-{:02}-{:02}_{:02}-{:02}-{:02}-{:03}-p{}.log", role,
                                       local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                                       local.tm_min, local.tm_sec, milliseconds.count() % 1000,
                                       spdlog::details::os::pid());
    }

    // --------------------------------------------------------------------------
    // Logger: serialize the session's fan-out, configuration and final flush
    // --------------------------------------------------------------------------
    Logger& Logger::get_instance()
    {
        static Logger instance;
        return instance;
    }

    bool Logger::init(const LogConfig& config, std::string* error_message)
    {
        // Initialization/reconfiguration belongs to the quiescent composition root.
        exit();
        std::lock_guard<std::mutex> lock(mutex_);
        if (error_message)
        {
            error_message->clear();
        }
        const auto buffer = config.memory_output;
        // C++17 filesystem keeps UTF-8 boundary inputs lossless through native file I/O.
        const std::filesystem::path file_path = config.log_directory / std::filesystem::u8path(config.file_name);
        if (buffer)
        {
            buffer->configure_file(config.file_output, file_path.u8string(), false);
        }
        std::vector<spdlog::sink_ptr> sinks;
        std::string failure;
        if (buffer)
        {
            sinks.push_back(std::make_shared<MemoryLogSink>(buffer));
        }
        if (config.console_output)
        {
            try
            {
                auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v");
                sinks.push_back(std::move(console));
            }
            catch (const std::exception& error)
            {
                failure = error.what();
                report_output_error(buffer, failure, false);
            }
        }
        if (config.file_output)
        {
            try
            {
                if (config.log_directory.empty() || config.file_name.empty() || config.max_file_size == 0 ||
                    config.max_file_count == 0)
                {
                    throw spdlog::spdlog_ex("Logger file output configuration is incomplete.");
                }
                // filesystem creates the native directory; failures retain the other outputs.
                std::error_code error;
                std::filesystem::create_directories(config.log_directory, error);
                if (error)
                {
                    throw spdlog::spdlog_ex("Failed to create log directory: " + error.message());
                }
                sinks.push_back(std::make_shared<FileLogSink>(file_path, config));
                if (buffer)
                {
                    buffer->configure_file(true, file_path.u8string(), true);
                }
            }
            catch (const std::exception& error)
            {
                if (!failure.empty())
                {
                    failure += "\n";
                }
                failure += error.what();
                report_output_error(buffer, error.what(), true);
            }
        }
        if (sinks.empty())
        {
            if (failure.empty())
            {
                failure = "Logger requires at least one output sink.";
            }
            // Keep future diagnostics visible even if a file-only setup failed.
            sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
        }
        logger_name = config.logger_name.empty() ? "toy3d" : config.logger_name;
        spd_logger = std::make_shared<spdlog::logger>(logger_name, sinks.begin(), sinks.end());
        spd_logger->set_level(spdlog::level::trace);
        spd_logger->flush_on(spdlog::level::warn);
        spd_logger->set_error_handler(
            [buffer](const std::string& message)
            {
                report_output_error(buffer, message, false);
            });
        try
        {
            spdlog::register_logger(spd_logger);
            registered_ = true;
        }
        catch (const spdlog::spdlog_ex& error)
        {
            if (!failure.empty())
            {
                failure += "\n";
            }
            failure += error.what();
            report_output_error(buffer, error.what(), false);
        }
        if (error_message)
        {
            *error_message = failure;
        }
        return failure.empty();
    }

    void Logger::set_level(Level level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!spd_logger)
        {
            return;
        }
        switch (level)
        {
        case Level::TOY_TRACE:
            spd_logger->set_level(spdlog::level::trace);
            break;
        case Level::TOY_DEBUG:
            spd_logger->set_level(spdlog::level::debug);
            break;
        case Level::TOY_INFO:
            spd_logger->set_level(spdlog::level::info);
            break;
        case Level::TOY_WARN:
            spd_logger->set_level(spdlog::level::warn);
            break;
        case Level::TOY_ERROR:
            spd_logger->set_level(spdlog::level::err);
            break;
        case Level::TOY_CRITICAL:
            spd_logger->set_level(spdlog::level::critical);
            break;
        case Level::TOY_OFF:
            spd_logger->set_level(spdlog::level::off);
            break;
        }
    }

    void Logger::exit()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (spd_logger)
        {
            spd_logger->flush();
            spd_logger.reset();
            if (registered_)
            {
                spdlog::drop(logger_name);
            }
        }
        registered_ = false;
        logger_name.clear();
    }
} // namespace toy3d
