#pragma once

#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace toy3d
{
    struct LogConfig
    {
        std::string logger_name = "toy3d";
        // filesystem::path preserves native path composition for the logging
        // backend instead of rebuilding platform separators as strings.
        std::filesystem::path log_directory;
        std::string file_name;
        bool console_output = true;
        bool file_output = true;
        std::size_t max_file_size = 10u * 1024u * 1024u;
        std::size_t max_file_count = 5u;
    };

    class Logger
    {
      public:
        enum class Level : std::uint8_t
        {
            TOY_TRACE,
            TOY_DEBUG,
            TOY_INFO,
            TOY_WARN,
            TOY_ERROR,
            TOY_CRITICAL,
            TOY_OFF
        };

        static Logger& get_instance();

        bool init(const LogConfig& config, std::string* error_message = nullptr);
        void set_level(Level level);

        template <typename... Args> void trace(const char* file, int line, const std::string& fmt, const Args&... args);

        template <typename... Args> void debug(const char* file, int line, const std::string& fmt, const Args&... args);

        template <typename... Args> void info(const char* file, int line, const std::string& fmt, const Args&... args);

        template <typename... Args> void warn(const char* file, int line, const std::string& fmt, const Args&... args);

        template <typename... Args> void error(const char* file, int line, const std::string& fmt, const Args&... args);

        template <typename... Args>
        void critical(const char* file, int line, const std::string& fmt, const Args&... args);

        void exit();

      private:
        Logger() = default;
        ~Logger() = default;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        std::string logger_name;
        std::shared_ptr<spdlog::logger> spd_logger;
    };

    template <typename... Args>
    void Logger::trace(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger)
            spd_logger->log(spdlog::source_loc{file, line, ""}, spdlog::level::trace, fmt, args...);
    }

    template <typename... Args>
    void Logger::debug(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger)
            spd_logger->log(spdlog::source_loc{file, line, ""}, spdlog::level::debug, fmt, args...);
    }

    template <typename... Args>
    void Logger::info(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger)
            spd_logger->log(spdlog::source_loc{file, line, ""}, spdlog::level::info, fmt, args...);
    }

    template <typename... Args>
    void Logger::warn(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger)
            spd_logger->log(spdlog::source_loc{file, line, ""}, spdlog::level::warn, fmt, args...);
    }

    template <typename... Args>
    void Logger::error(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger)
            spd_logger->log(spdlog::source_loc{file, line, ""}, spdlog::level::err, fmt, args...);
    }

    template <typename... Args>
    void Logger::critical(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger)
            spd_logger->log(spdlog::source_loc{file, line, ""}, spdlog::level::critical, fmt, args...);
    }
} // namespace toy3d

#define TOY_LOG_TRACE(...) ::toy3d::Logger::get_instance().trace(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_DEBUG(...) ::toy3d::Logger::get_instance().debug(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_INFO(...) ::toy3d::Logger::get_instance().info(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_WARN(...) ::toy3d::Logger::get_instance().warn(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_ERROR(...) ::toy3d::Logger::get_instance().error(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_CRITICAL(...) ::toy3d::Logger::get_instance().critical(__FILE__, __LINE__, __VA_ARGS__)
