#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include "file_system/file_error.h"

namespace toy3d
{
    class FileSystem;
    struct FileStatus;
    class VirtualPath;

    enum class ConfigLoadMode
    {
        Replace,
        Overlay
    };

    struct ConfigValue
    {
        std::string value;
        std::string source;
        std::size_t line = 0;
    };
    using ConfigValues = std::map<std::string, ConfigValue>;

    class ConsoleManager
    {
      public:
        static ConsoleManager& get_instance();

        ConsoleManager(const ConsoleManager&) = delete;
        ConsoleManager& operator=(const ConsoleManager&) = delete;
        ConsoleManager(ConsoleManager&&) = delete;
        ConsoleManager& operator=(ConsoleManager&&) = delete;

        FileStatus load_config(FileSystem& file_system, const VirtualPath& path,
                               ConfigLoadMode mode = ConfigLoadMode::Replace);
        static FileResult<ConfigValues> parse_config(const std::string& text, const std::string& source);
        static FileResult<std::string> encode_config(const ConfigValues& values);
        void set_value(const std::string& key, const std::string& value, const std::string& source = "runtime");
        ConfigValues snapshot() const;
        std::string get_string(const std::string& key, const std::string& default_value = "") const;
        int get_int(const std::string& key, int default_value = 0) const;
        float get_float(const std::string& key, float default_value = 0.0f) const;
        bool get_bool(const std::string& key, bool default_value = false) const;

        void parse_resolution(const std::string& value, int& width, int& height) const;
        void reset_for_tests();

      private:
        ConsoleManager() = default;

        // shared_mutex allows concurrent read-only CVar queries while keeping
        // registration and mutation exclusive under the same lock.
        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, std::string> values_;
        ConfigValues origins_;
    };
} // namespace toy3d
