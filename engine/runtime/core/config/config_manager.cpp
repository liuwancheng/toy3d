#include "config_manager.h"

namespace toy3d
{

    ConfigManager& ConfigManager::get_instance() 
    {
        static ConfigManager _instance;
        return _instance;
    }

    bool ConfigManager::load_config_file(const std::string& filename) 
    {
        std::ifstream file(filename);
        if (!file.is_open()) 
        {
            return false;
        }
        
        std::string line, section;
        
        while (std::getline(file, line)) 
        {
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            
            if (line.empty() || line[0] == ';' || line[0] == '#') 
            {
                continue;
            }
            
            if (line[0] == '[' && line.back() == ']') 
            {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            
            size_t pos = line.find('=');
            if (pos != std::string::npos) 
            {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                
                size_t commentPos = value.find('#');
                if (commentPos != std::string::npos) {
                    value = value.substr(0, commentPos);
                    value.erase(value.find_last_not_of(" \t") + 1);
                }
                
                if (!section.empty()) 
                {
                    config_values[section + "." + key] = value;
                } 
                else 
                {
                    config_values[key] = value;
                }
            }
        }
        
        return true;
    }

    void ConfigManager::set_value(const std::string& key, const std::string& value) 
    {
        config_values[key] = value;
    }

    std::string ConfigManager::get_str(const std::string& key, const std::string& defaultValue) const 
    {
        auto it = config_values.find(key);
        return (it != config_values.end()) ? it->second : defaultValue;
    }

    int ConfigManager::get_int(const std::string& key, int defaultValue) const 
    {
        auto it = config_values.find(key);
        if (it != config_values.end()) 
        {
            try 
            {
                return std::stoi(it->second);
            } 
            catch (...) 
            {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    float ConfigManager::get_float(const std::string& key, float defaultValue) const 
    {
        auto it = config_values.find(key);
        if (it != config_values.end()) 
        {
            try 
            {
                return std::stof(it->second);
            } 
            catch (...) 
            {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    bool ConfigManager::get_bool(const std::string& key, bool defaultValue) const 
    {
        auto it = config_values.find(key);
        if (it != config_values.end()) 
        {
            std::string value = it->second;
            std::transform(value.begin(), value.end(), value.begin(), ::tolower);
            return (value == "true" || value == "1" || value == "yes" || value == "on");
        }
        return defaultValue;
    }

    void ConfigManager::parse_resolution(const std::string& res, int& width, int& height) const 
    {
        size_t pos = res.find('x');
        if (pos != std::string::npos) 
        {
            try {
                width = std::stoi(res.substr(0, pos));
                height = std::stoi(res.substr(pos + 1));
            } catch (...) 
            {
                width = 1280; // 默认宽度
                height = 720; // 默认高度
            }
        }
    }

} // namespace toy3d