#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>

namespace toy3d {

    class Logger 
    {
    public:
        enum class Level : uint8_t 
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
    
        void init(const std::string& file_name, bool console_output = true);
    
        void set_level(Level level);
 
        template<typename... Args>
        void trace(const std::string& fmt, const Args&... args);
    
        template<typename... Args>
        void debug(const std::string& fmt, const Args&... args);
    
        template<typename... Args>
        void info(const std::string& fmt, const Args&... args);
    
        template<typename... Args>
        void warn(const std::string& fmt, const Args&... args);
    
        template<typename... Args>
        void error(const std::string& fmt, const Args&... args);
    
        template<typename... Args>
        void critical(const std::string& fmt, const Args&... args);
    
        void exit();
    private:
        Logger() = default;
        ~Logger() = default;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
    
        std::shared_ptr<spdlog::logger> spd_logger_;
    };

    // 模板函数实现
    template<typename... Args>
    void Logger::trace(const std::string& fmt, const Args&... args) 
    {
        if (spd_logger_)
            spd_logger_->trace(fmt, args...);
    }

    template<typename... Args>
    void Logger::debug(const std::string& fmt, const Args&... args) 
    {
        if (spd_logger_)
            spd_logger_->debug(fmt, args...);
    }

    template<typename... Args>
    void Logger::info(const std::string& fmt, const Args&... args) 
    {
        if (spd_logger_)
            spd_logger_->info(fmt, args...);
    }

    template<typename... Args>
    void Logger::warn(const std::string& fmt, const Args&... args) 
    {
        if (spd_logger_)
            spd_logger_->warn(fmt, args...);
    }

    template<typename... Args>
    void Logger::error(const std::string& fmt, const Args&... args) 
    {
        if (spd_logger_)
            spd_logger_->error(fmt, args...);
    }

    template<typename... Args>
    void Logger::critical(const std::string& fmt, const Args&... args) 
    {
        if (spd_logger_)
            spd_logger_->critical(fmt, args...);
    }

} // namespace toy3d

// 便捷宏定义 - 同时支持普通消息和格式化消息
#define TOY_LOG_TRACE(...) ::toy3d::Logger::get_instance().trace(__VA_ARGS__)
#define TOY_LOG_DEBUG(...) ::toy3d::Logger::get_instance().debug(__VA_ARGS__)
#define TOY_LOG_INFO(...) ::toy3d::Logger::get_instance().info(__VA_ARGS__)
#define TOY_LOG__WARN(...) ::toy3d::Logger::get_instance().warn(__VA_ARGS__)
#define TOY_LOG_ERROR(...) ::toy3d::Logger::get_instance().error(__VA_ARGS__)
#define TOY_LOG_CRITICAL(...) ::toy3d::Logger::get_instance().critical(__VA_ARGS__)