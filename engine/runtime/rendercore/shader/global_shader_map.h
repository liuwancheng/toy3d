#pragma once

#include "rendercore/shader/global_shader_type.h"
#include "rendercore/shader/shader_map.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace toy3d
{
    class GlobalShaderMap;

    struct GlobalShaderMapResult
    {
        std::shared_ptr<const GlobalShaderMap> shader_map;
        std::string error;

        bool succeeded() const { return shader_map != nullptr && error.empty(); }
    };

    class GlobalShaderMap final
    {
    public:
        GlobalShaderMap(GlobalShaderMap&&) noexcept = default;
        GlobalShaderMap& operator=(GlobalShaderMap&&) noexcept = default;
        GlobalShaderMap(const GlobalShaderMap&) = delete;
        GlobalShaderMap& operator=(const GlobalShaderMap&) = delete;

        static GlobalShaderMapResult load(
            ShaderMap& shader_map,
            ShaderPlatform platform,
            const std::vector<const GlobalShaderType*>& required_types);

        ShaderMapProgramResult find(const GlobalShaderType& type) const;
        ShaderPlatform platform() const { return platform_; }
        std::size_t size() const { return programs_.size(); }

    private:
        struct Entry
        {
            GlobalShaderType type;
            ShaderMapProgramRef program;
        };

        explicit GlobalShaderMap(ShaderPlatform platform)
            : platform_(platform)
        {
        }

        ShaderPlatform platform_;
        std::unordered_map<std::string, Entry> programs_;
    };
}
