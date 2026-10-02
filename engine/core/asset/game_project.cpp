#include "asset/game_project.h"

#include <algorithm>
#include <set>
#include "asset/yaml_validation.h"

namespace toy3d
{
    namespace
    {
        constexpr std::size_t maximum_project_bytes = 64u * 1024u;
        FileStatus invalid_project(const std::string& error)
        { return {FileErrorCode::InvalidData, "game_project", {}, {}, error}; }
        bool known_fields(const YAML::Node& node, const std::set<std::string>& allowed)
        {
            if (!node.IsMap()) return false;
            for (const auto& item : node) if (!allowed.count(item.first.Scalar())) return false;
            return true;
        }
    }

    bool valid_project_name(const std::string& name)
    {
        const auto letter = [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        if (name.empty() || name.size() > 64u || !letter(name.front())) return false;
        if (!std::all_of(name.begin(), name.end(), [&](unsigned char c)
            { return letter(c) || (c >= '0' && c <= '9') || c == '_'; })) return false;
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c)
            { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c); });
        const std::set<std::string> reserved = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
            "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
        return !reserved.count(upper);
    }

    FileResult<GameProject> parse_game_project(const std::string& text)
    {
        if (text.size() > maximum_project_bytes || !is_valid_utf8(text) || text.find('\0') != std::string::npos)
            return FileResult<GameProject>(invalid_project("Project YAML is oversized or invalid UTF-8."));
        try
        {
            const auto documents = YAML::LoadAll(text);
            if (documents.size() != 1u) return FileResult<GameProject>(invalid_project("Expected one project YAML document."));
            const auto& root = documents.front();
            ValueLimits limits; limits.max_depth = 8u; limits.max_array_elements = 128u; limits.max_string_bytes = 1024u;
            std::size_t count = 0; std::set<std::pair<int, int>> marks; std::string error;
            if (!check_yaml_tree(root, 0u, count, marks, limits, error)) return FileResult<GameProject>(invalid_project(error));
            if (!known_fields(root, {"format_version", "project_id", "name", "engine_association", "modules"}) ||
                !root["format_version"] || root["format_version"].as<std::string>() != "1")
                return FileResult<GameProject>(invalid_project("Unknown project fields or unsupported format_version."));
            GameProject project;
            const std::string id = root["project_id"].as<std::string>();
            project.name = root["name"].as<std::string>();
            project.engine_association = root["engine_association"].as<std::string>();
            if (!AssetId::parse(id, project.id) || project.id.hex() != id || !valid_project_name(project.name) ||
                !valid_project_name(project.engine_association))
                return FileResult<GameProject>(invalid_project("Invalid project_id, name or engine_association."));
            const auto modules = root["modules"];
            if (modules)
            {
                if (!modules.IsSequence() || modules.size() > 2u) return FileResult<GameProject>(invalid_project("Invalid modules list."));
                std::set<std::string> names, types;
                for (const auto& item : modules)
                {
                    if (!known_fields(item, {"name", "type"})) return FileResult<GameProject>(invalid_project("Unknown module field."));
                    GameModule module; module.name = item["name"].as<std::string>();
                    const std::string type = item["type"].as<std::string>();
                    if (!valid_project_name(module.name) || !names.insert(module.name).second || !types.insert(type).second ||
                        (type != "Runtime" && type != "Editor")) return FileResult<GameProject>(invalid_project("Invalid or duplicate module."));
                    module.type = type == "Runtime" ? GameModuleType::Runtime : GameModuleType::Editor;
                    project.modules.push_back(std::move(module));
                }
                if (!project.modules.empty() && !types.count("Runtime"))
                    return FileResult<GameProject>(invalid_project("An Editor module requires a Runtime module."));
            }
            return FileResult<GameProject>(std::move(project));
        }
        catch (const YAML::Exception& exception)
        { return FileResult<GameProject>(invalid_project(std::string("Project YAML: ") + exception.what())); }
    }

    FileResult<std::string> encode_game_project(const GameProject& project)
    {
        for (const auto& module : project.modules)
            if (module.type != GameModuleType::Runtime && module.type != GameModuleType::Editor)
                return FileResult<std::string>(invalid_project("Invalid module type."));
        YAML::Emitter yaml;
        yaml << YAML::BeginMap << YAML::Key << "format_version" << YAML::Value << 1
             << YAML::Key << "project_id" << YAML::Value << project.id.hex()
             << YAML::Key << "name" << YAML::Value << project.name
             << YAML::Key << "engine_association" << YAML::Value << project.engine_association
             << YAML::Key << "modules" << YAML::Value << YAML::BeginSeq;
        for (const auto& module : project.modules)
            yaml << YAML::BeginMap << YAML::Key << "name" << YAML::Value << module.name
                 << YAML::Key << "type" << YAML::Value << (module.type == GameModuleType::Runtime ? "Runtime" : "Editor") << YAML::EndMap;
        yaml << YAML::EndSeq << YAML::EndMap;
        if (!yaml.good()) return FileResult<std::string>(invalid_project(yaml.GetLastError()));
        const std::string text = std::string(yaml.c_str()) + "\n";
        const auto checked = parse_game_project(text);
        return checked.succeeded() ? FileResult<std::string>(text) : FileResult<std::string>(checked.status());
    }

    FileResult<GameProject> read_game_project(const FileSystem& files, const VirtualPath& path)
    {
        const auto text = files.read_text_utf8(path, maximum_project_bytes);
        if (!text.succeeded()) return FileResult<GameProject>(text.status());
        auto result = parse_game_project(text.value());
        if (result.succeeded()) return result;
        auto status = result.status(); status.virtual_path = path.utf8();
        return FileResult<GameProject>(std::move(status));
    }
}
