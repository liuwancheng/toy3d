#include "console_manager.h"

#include "file_system/file_system.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include "misc/utf8.h"
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

    FileResult<ConfigValues> ConsoleManager::parse_config(const std::string& text, const std::string& source)
    {
        constexpr std::size_t maximum_config_bytes = 256u * 1024u;
        ConfigValues parsed;
        std::string section;
        std::size_t offset = 0, line_number = 0;
        const auto fail = [&](const std::string& message)
        {
            FileStatus status;
            status.code = FileErrorCode::InvalidData; status.operation = "parse_config";
            status.virtual_path = source;
            status.message = source + ":" + std::to_string(line_number) + ": " + message;
            return FileResult<ConfigValues>(std::move(status));
        };
        if (text.size() > maximum_config_bytes || !is_valid_utf8(text) || text.find('\0') != std::string::npos)
            return fail("Config is oversized or invalid UTF-8.");
        while (offset <= text.size())
        {
            ++line_number;
            const auto end = text.find('\n', offset);
            std::string line = text.substr(offset, end == std::string::npos ? std::string::npos : end - offset);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            trim(line);
            if (!line.empty() && line.front() != ';' && line.front() != '#')
            {
                if (line.front() == '[')
                {
                    if (line.back() != ']') return fail("Malformed section.");
                    section = line.substr(1u, line.size() - 2u); trim(section);
                    if (section.empty()) return fail("Empty section.");
                }
                else
                {
                    const auto separator = line.find('=');
                    if (separator == std::string::npos) return fail("Expected key=value.");
                    std::string key = line.substr(0u, separator), value = line.substr(separator + 1u);
                    trim(key); trim(value);
                    if (key.empty()) return fail("Empty key.");
                    if (!value.empty() && (value.front() == '"' || value.front() == '\''))
                    {
                        const auto closing = value.find(value.front(), 1u);
                        if (closing == std::string::npos) return fail("Unclosed quoted value.");
                        std::string tail = value.substr(closing + 1u); trim(tail);
                        if (!tail.empty() && tail.front() != '#' && tail.front() != ';')
                            return fail("Unexpected text after quoted value.");
                        value = value.substr(1u, closing - 1u);
                    }
                    else
                    {
                        for (std::size_t i = 0; i < value.size(); ++i)
                            if ((value[i] == '#' || value[i] == ';') &&
                                (i == 0u || std::isspace(static_cast<unsigned char>(value[i - 1u]))))
                            { value.resize(i); break; }
                        trim(value);
                    }
                    const std::string full_key = section.empty() ? key : section + "." + key;
                    if (!parsed.emplace(full_key, ConfigValue{value, source, line_number}).second)
                        return fail("Duplicate key: " + full_key);
                    try
                    {
                        if (full_key == "Window.Width" || full_key == "Window.Height" || full_key == "Renderer.MSAA")
                        {
                            std::size_t consumed = 0; const int number = std::stoi(value, &consumed);
                            if (consumed != value.size() || number <= 0 || number > 16384 ||
                                (full_key == "Renderer.MSAA" && number != 1 && number != 2 && number != 4 && number != 8))
                                return fail("Invalid value for " + full_key);
                        }
                        if (full_key == "Window.Fullscreen" || full_key == "Renderer.VSync" ||
                            full_key == "Renderer.MultiThreaded" || full_key == "Debug.EnableValidation")
                        {
                            std::string lower = value;
                            std::transform(lower.begin(), lower.end(), lower.begin(),
                                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (lower != "true" && lower != "false" && lower != "1" && lower != "0" &&
                                lower != "yes" && lower != "no" && lower != "on" && lower != "off")
                                return fail("Invalid boolean for " + full_key);
                        }
                    }
                    catch (const std::exception&) { return fail("Invalid value for " + full_key); }
                }
            }
            if (end == std::string::npos) break;
            offset = end + 1u;
        }
        return FileResult<ConfigValues>(std::move(parsed));
    }

    FileResult<std::string> ConsoleManager::encode_config(const ConfigValues& values)
    {
        std::map<std::string, std::map<std::string, std::string>> sections;
        for (const auto& item : values)
        {
            const auto dot = item.first.find_last_of('.');
            sections[dot == std::string::npos ? "" : item.first.substr(0u, dot)]
                [dot == std::string::npos ? item.first : item.first.substr(dot + 1u)] = item.second.value;
        }
        std::string text;
        for (const auto& section : sections)
        {
            if (!section.first.empty()) text += "[" + section.first + "]\n";
            for (const auto& key : section.second)
            {
                std::string value = key.second;
                if (value.find('"') == std::string::npos) value = "\"" + value + "\"";
                else if (value.find('\'') == std::string::npos) value = "'" + value + "'";
                text += key.first + "=" + value + "\n";
            }
            text += "\n";
        }
        const auto parsed = parse_config(text, "encode_config");
        if (!parsed.succeeded()) return FileResult<std::string>(parsed.status());
        if (parsed.value().size() != values.size())
            return FileResult<std::string>(FileStatus{FileErrorCode::InvalidData, "encode_config", {}, {}, "Config field count cannot round-trip."});
        for (const auto& item : values)
        {
            const auto found = parsed.value().find(item.first);
            if (found == parsed.value().end() || found->second.value != item.second.value)
                return FileResult<std::string>(FileStatus{FileErrorCode::InvalidData, "encode_config", {}, {}, "Config values cannot round-trip."});
        }
        return FileResult<std::string>(std::move(text));
    }

    FileStatus ConsoleManager::load_config(FileSystem& files, const VirtualPath& path, ConfigLoadMode mode)
    {
        const auto text = files.read_text_utf8(path, 256u * 1024u);
        if (!text.succeeded()) return text.status();
        const auto parsed = parse_config(text.value(), path.utf8());
        if (!parsed.succeeded()) return parsed.status();
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (mode == ConfigLoadMode::Replace) { values_.clear(); origins_.clear(); }
        for (const auto& item : parsed.value())
        { values_[item.first] = item.second.value; origins_[item.first] = item.second; }
        return FileStatus::success();
    }

    void ConsoleManager::set_value(const std::string& key, const std::string& value, const std::string& source)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        values_[key] = value; origins_[key] = {value, source, 0u};
    }

    ConfigValues ConsoleManager::snapshot() const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return origins_;
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
            std::size_t end = 0; const int parsed = std::stoi(value, &end);
            return end == value.size() ? parsed : default_value;
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
            std::size_t end = 0; const float parsed = std::stof(value, &end);
            return end == value.size() && std::isfinite(parsed) ? parsed : default_value;
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
        origins_.clear();
    }
} // namespace toy3d
