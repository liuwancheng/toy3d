#pragma once
#include "core/misc/pch.h"

namespace toy3d
{

    class ConfigManager 
    {
    public:
        ConfigManager() = default;
        ConfigManager(const ConfigManager&) = delete;
        ConfigManager& operator=(const ConfigManager&) = delete;
        ConfigManager(ConfigManager&&) = delete;
    public:
        static ConfigManager& get_instance();
        
        bool load_config_file(const std::string& filename);
        void set_value(const std::string& key, const std::string& value);
        std::string get_str(const std::string& key, const std::string& defaultValue = "") const;
        int get_int(const std::string& key, int defaultValue = 0) const;
        float get_float(const std::string& key, float defaultValue = 0.0f) const;
        bool get_bool(const std::string& key, bool defaultValue = false) const;
        
        // 解析分辨率字符串，如"1920x1080"
        void parse_resolution(const std::string& res, int& width, int& height) const;
        
    private:
        std::unordered_map<std::string, std::string> config_values;
    };

}// namespace toy3d
