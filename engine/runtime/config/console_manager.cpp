#include "console_manager.h"

#include "file_system/file_system.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <utility>

namespace toy3d
{
    // shared_mutex lock modes below allow concurrent reads while serializing
    // registration and mutation of the same ConsoleManager state.
    namespace
    {
        void trim(std::string& value)
        {
            const std::size_t first = value.find_first_not_of(" \t");
            if (first == std::string::npos)
            {
                value.clear();
                return;
            }
            const std::size_t last = value.find_last_not_of(" \t");
            value = value.substr(first, last - first + 1);
        }
    } // namespace

    ConsoleManager& ConsoleManager::get_instance()
    {
        static ConsoleManager instance;
        return instance;
    }

    FileStatus ConsoleManager::load_config(FileSystem& file_system, const VirtualPath& path, ConfigLoadMode mode)
    {
        auto text = file_system.read_text_utf8(path);
        if (!text.succeeded())
        {
            return text.status();
        }

        std::unordered_map<std::string, std::string> parsed_values;
        std::string line;
        std::string section;
        std::size_t offset = 0;
        const std::string& source = text.value();
        while (offset <= source.size())
        {
            const std::size_t newline = source.find('\n', offset);
            line = source.substr(offset, newline == std::string::npos ? std::string::npos : newline - offset);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            trim(line);

            if (line.empty() || line[0] == ';' || line[0] == '#')
            {
                if (newline == std::string::npos)
                    break;
                offset = newline + 1;
                continue;
            }

            if (line[0] == '[' && line.back() == ']')
            {
                section = line.substr(1, line.size() - 2);
                trim(section);
                if (newline == std::string::npos)
                    break;
                offset = newline + 1;
                continue;
            }

            const std::size_t separator = line.find('=');
            if (separator != std::string::npos)
            {
                std::string key = line.substr(0, separator);
                std::string value = line.substr(separator + 1);
                trim(key);
                trim(value);

                const std::size_t comment = value.find('#');
                if (comment != std::string::npos)
                {
                    value = value.substr(0, comment);
                    trim(value);
                }

                if (!key.empty() && !section.empty())
                {
                    parsed_values[section + "." + key] = value;
                }
                else if (!key.empty())
                {
                    parsed_values[key] = value;
                }
            }

            if (newline == std::string::npos)
                break;
            offset = newline + 1;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (mode == ConfigLoadMode::Replace)
        {
            values_ = std::move(parsed_values);
        }
        else
        {
            // Publish the complete layer under one lock; omitted project keys
            // retain their engine defaults. Failed reads never reach this point.
            for (const auto& entry : parsed_values)
                values_[entry.first] = entry.second;
        }
        return FileStatus::success();
    }

    void ConsoleManager::set_value(const std::string& key, const std::string& value)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        values_[key] = value;
    }

    std::string ConsoleManager::get_string(const std::string& key, const std::string& default_value) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto found = values_.find(key);
        return found != values_.end() ? found->second : default_value;
    }

    int ConsoleManager::get_int(const std::string& key, int default_value) const
    {
        const std::string value = get_string(key);
        if (value.empty())
            return default_value;
        try
        {
            return std::stoi(value);
        }
        catch (...)
        {
            return default_value;
        }
    }

    float ConsoleManager::get_float(const std::string& key, float default_value) const
    {
        const std::string value = get_string(key);
        if (value.empty())
            return default_value;
        try
        {
            return std::stof(value);
        }
        catch (...)
        {
            return default_value;
        }
    }

    bool ConsoleManager::get_bool(const std::string& key, bool default_value) const
    {
        std::string value = get_string(key);
        if (value.empty())
            return default_value;
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (value == "true" || value == "1" || value == "yes" || value == "on")
            return true;
        if (value == "false" || value == "0" || value == "no" || value == "off")
            return false;
        return default_value;
    }

    void ConsoleManager::parse_resolution(const std::string& value, int& width, int& height) const
    {
        const std::size_t separator = value.find('x');
        if (separator == std::string::npos)
            return;
        try
        {
            width = std::stoi(value.substr(0, separator));
            height = std::stoi(value.substr(separator + 1));
        }
        catch (...)
        {
            width = 1280;
            height = 720;
        }
    }

    void ConsoleManager::reset_for_tests()
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        values_.clear();
    }
} // namespace toy3d
