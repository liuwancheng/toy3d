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
        void trace(const char* file, int line, const std::string& fmt, const Args&... args);
        
        template<typename... Args>
        void debug(const char* file, int line, const std::string& fmt, const Args&... args);
        
        template<typename... Args>
        void info(const char* file, int line, const std::string& fmt, const Args&... args);
        
        template<typename... Args>
        void warn(const char* file, int line, const std::string& fmt, const Args&... args);
        
        template<typename... Args>
        void error(const char* file, int line, const std::string& fmt, const Args&... args);
        
        template<typename... Args>
        void critical(const char* file, int line, const std::string& fmt, const Args&... args);
    
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
    void Logger::trace(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger_)
            spd_logger_->log(spdlog::source_loc{file, line, ""}, spdlog::level::trace, fmt, args...);
    }

    template<typename... Args>
    void Logger::debug(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger_)
            spd_logger_->log(spdlog::source_loc{file, line, ""}, spdlog::level::debug, fmt, args...);
    }

    template<typename... Args>
    void Logger::info(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger_)
            spd_logger_->log(spdlog::source_loc{file, line, ""}, spdlog::level::info, fmt, args...);
    }

    template<typename... Args>
    void Logger::warn(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger_)
            spd_logger_->log(spdlog::source_loc{file, line, ""}, spdlog::level::warn, fmt, args...);
    }

    template<typename... Args>
    void Logger::error(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger_)
            spd_logger_->log(spdlog::source_loc{file, line, ""}, spdlog::level::err, fmt, args...);
    }

    template<typename... Args>
    void Logger::critical(const char* file, int line, const std::string& fmt, const Args&... args)
    {
        if (spd_logger_)
            spd_logger_->log(spdlog::source_loc{file, line, ""}, spdlog::level::critical, fmt, args...);
    }

} // namespace toy3d


#define TOY_LOG_TRACE(...) ::toy3d::Logger::get_instance().trace(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_DEBUG(...) ::toy3d::Logger::get_instance().debug(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_INFO(...) ::toy3d::Logger::get_instance().info(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_WARN(...) ::toy3d::Logger::get_instance().warn(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_ERROR(...) ::toy3d::Logger::get_instance().error(__FILE__, __LINE__, __VA_ARGS__)
#define TOY_LOG_CRITICAL(...) ::toy3d::Logger::get_instance().critical(__FILE__, __LINE__, __VA_ARGS__)