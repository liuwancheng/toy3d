#pragma once

#include "asset/asset_identity.h"
#include "file_system/file_system.h"

#include <string>
#include <vector>

namespace toy3d
{
    enum class GameModuleType
    {
        Runtime,
        Editor
    };
    struct GameModule
    {
        std::string name;
        GameModuleType type = GameModuleType::Runtime;
    };
    struct GameProject
    {
        AssetId id;
        std::string name;
        std::string engine_association;
        std::vector<GameModule> modules;
    };

    bool valid_project_name(const std::string& name);
    FileResult<GameProject> parse_game_project(const std::string& yaml);
    FileResult<std::string> encode_game_project(const GameProject& project);
    FileResult<GameProject> read_game_project(const FileSystem& files, const VirtualPath& path);
} // namespace toy3d
