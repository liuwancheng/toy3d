#include "logger.h"
#include <iostream>
#include <filesystem>
#include "generated/defines.h"

namespace toy3d {

Logger& Logger::get_instance() 
{
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& file_name, bool console_output) 
{
    std::vector<spdlog::sink_ptr> sinks;

    // 添加控制台输出
    if (console_output) 
    {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
        //console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        sinks.push_back(console_sink);
    }

    // 添加文件输出
    if (!file_name.empty()) 
    {
        std::filesystem::path saved_root = ENGINE_SAVED_ROOT;
        std::filesystem::path abs_log_path = saved_root / file_name;
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(abs_log_path.string(), true);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        sinks.push_back(file_sink);
    }

    if (!sinks.empty()) 
    {
        spd_logger = std::make_shared<spdlog::logger>("toy3d", sinks.begin(), sinks.end());
        spd_logger->set_level(spdlog::level::trace);
        spd_logger->flush_on(spdlog::level::warn);
        spdlog::register_logger(spd_logger);
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
        spdlog::drop_all();
        spdlog::shutdown();
    }
}

} // namespace toy3d