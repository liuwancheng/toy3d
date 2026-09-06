#include "logging/logger.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <utility>
#include <vector>

namespace toy3d
{
    // filesystem performs native directory creation and path joining here so
    // logging does not duplicate platform separator and error-code handling.
    Logger& Logger::get_instance()
    {
        static Logger instance;
        return instance;
    }

    bool Logger::init(const LogConfig& config, std::string* error_message)
    {
        exit();

        if (!config.console_output && !config.file_output)
        {
            if (error_message)
                *error_message = "Logger requires at least one output sink.";
            return false;
        }
        if (config.file_output && (config.log_directory.empty() || config.file_name.empty() ||
                                   config.max_file_size == 0 || config.max_file_count == 0))
        {
            if (error_message)
                *error_message = "Logger file output configuration is incomplete.";
            return false;
        }

        try
        {
            std::vector<spdlog::sink_ptr> sinks;
            if (config.console_output)
            {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v");
                sinks.push_back(std::move(console_sink));
            }

            if (config.file_output)
            {
                std::error_code directory_error;
                std::filesystem::create_directories(config.log_directory, directory_error);
                if (directory_error)
                {
                    if (error_message)
                    {
                        *error_message = "Failed to create log directory '" + config.log_directory.string() +
                                         "': " + directory_error.message();
                    }
                    return false;
                }

                const std::filesystem::path log_path = config.log_directory / config.file_name;
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    log_path.string(), config.max_file_size, config.max_file_count, false);
                file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
                sinks.push_back(std::move(file_sink));
            }

            logger_name = config.logger_name.empty() ? "toy3d" : config.logger_name;
            spd_logger = std::make_shared<spdlog::logger>(logger_name, sinks.begin(), sinks.end());
            spd_logger->set_level(spdlog::level::trace);
            spd_logger->flush_on(spdlog::level::warn);
            spdlog::register_logger(spd_logger);
            return true;
        }
        catch (const spdlog::spdlog_ex& error)
        {
            spd_logger.reset();
            logger_name.clear();
            if (error_message)
                *error_message = error.what();
            return false;
        }
    }

    void Logger::set_level(Level level)
    {
        if (!spd_logger)
            return;

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
        if (spd_logger)
        {
            spd_logger->flush();
            spd_logger.reset();
            spdlog::drop(logger_name);
        }
        logger_name.clear();
    }
} // namespace toy3d
