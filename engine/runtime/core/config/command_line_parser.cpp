// command_line_parser.cpp
#include "command_line_parser.h"
#include "config_manager.h"

namespace toy3d
{
    CommandLineParser& CommandLineParser::get_instance() 
    {
        static CommandLineParser _instance;
        return _instance;
    }


    void CommandLineParser::parser_args(const std::vector<std::string>& args) 
    {
        for (size_t i = 1; i < args.size(); ++i) 
        {
            const std::string& arg = args[i];
            
            // 1、处理--key=value格式
            if (arg.substr(0, 2) == "--") 
            {
                size_t equalPos = arg.find('=');
                if (equalPos != std::string::npos) 
                {
                    std::string key = arg.substr(2, equalPos - 2);
                    std::string value = arg.substr(equalPos + 1);
                    _options[key] = value;
                } 
                else 
                {
                    // 2、处理--flag格式的布尔标志
                    std::string key = arg.substr(2);
                    _options[key] = "true";
                }
            }
            else 
            {
                // 3、处理简单的key=value格式
                size_t equalPos = arg.find('=');
                if (equalPos != std::string::npos) 
                {
                    std::string key = arg.substr(0, equalPos);
                    std::string value = arg.substr(equalPos + 1);
                    _options[key] = value;
                }
            }
        }
    }

    void CommandLineParser::parser_args(int argc, char* argv[]) 
    {
        for (int i = 1; i < argc; ++i) 
        {
            std::string arg = argv[i];
            if (arg.substr(0, 2) == "--") 
            {
                size_t equalPos = arg.find('=');
                if (equalPos != std::string::npos) 
                {
                    std::string key = arg.substr(2, equalPos - 2);
                    std::string value = arg.substr(equalPos + 1);
                    _options[key] = value;
                } 
                else 
                {
                    std::string key = arg.substr(2);
                    _options[key] = "true";
                }
            }
            else 
            {
                size_t equalPos = arg.find('=');
                if (equalPos != std::string::npos) 
                {
                    std::string key = arg.substr(0, equalPos);
                    std::string value = arg.substr(equalPos + 1);
                    _options[key] = value;
                }
            }
        }
    }

    bool CommandLineParser::has_option(const std::string& option) const 
    {
        return _options.find(option) != _options.end();
    }

    std::string CommandLineParser::get_option(const std::string& option, const std::string& default_val) const 
    {
        auto it = _options.find(option);
        return (it != _options.end()) ? it->second : default_val;
    }

    void CommandLineParser::apply_config() const 
    {
        auto& config = ConfigManager::get_instance();
        
        for (const auto& [key, value] : _options) 
        {
            if (key == "resX" || key == "Width") 
            {
                config.set_value("Window.Width", value);
            }
            else if (key == "resY" || key == "Height") 
            {
                config.set_value("Window.Height", value);
            }
            else if (key == "fullscreen") 
            {
                config.set_value("Window.Mode", value);
            }
            else if (key == "vsync") 
            {
                config.set_value("Window.Vsync", value);
            }
            else 
            {
                config.set_value(key, value);
            }
        }
    }
} // namespace toy3d
