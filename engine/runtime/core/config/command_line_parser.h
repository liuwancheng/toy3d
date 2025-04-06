#pragma once
#include "core/misc/pch.h"

namespace toy3d
{
    // 命令行参数解析器
    // 支持--key=value格式和--flag格式的布尔标志
    class CommandLineParser 
    {
    public:
        CommandLineParser() = default;
        CommandLineParser(const CommandLineParser&) = delete;
        CommandLineParser& operator=(const CommandLineParser&) = delete;
        CommandLineParser(CommandLineParser&&) = delete;
        CommandLineParser& operator=(CommandLineParser&&) = delete;
    public:
        static CommandLineParser& get_instance();

        void parser_args(const std::vector<std::string>& args);
        void parser_args(int argc, char* argv[]);
        
        bool has_option(const std::string& option) const;
        std::string get_option(const std::string& option, const std::string& default_val = "") const;
        
        void apply_config() const;
    private:
        std::unordered_map<std::string, std::string> _options;
    };

} // namespace toy3d
