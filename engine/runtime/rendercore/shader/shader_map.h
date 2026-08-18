#pragma once

#include "rendercore/shader/shader_map_loader.h"
#include "rendercore/shader/shader_parameter_binding.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace toy3d
{
    class ShaderMapProgram final
    {
    public:
        struct ConstructionToken
        {
        private:
            ConstructionToken() = default;
            friend class ShaderMap;
        };

        ShaderMapProgram(ConstructionToken, ShaderMapProgramData data);

        const ShaderMapProgramData& data() const;
        const ShaderParameterBinding* find_parameter_binding(
            ShaderParameterId parameter_id) const;

    private:
        friend class ShaderMap;

        ShaderMapProgramData data_;
        std::unordered_map<ShaderParameterId, ShaderParameterBinding> parameter_bindings_;
    };

    using ShaderMapProgramRef = std::shared_ptr<const ShaderMapProgram>;

    struct ShaderMapProgramResult
    {
        ShaderMapProgramRef program;
        std::string error;

        bool succeeded() const;
    };

    class ShaderMap final
    {
    public:
        explicit ShaderMap(ShaderMapLoader& loader);

        ShaderMapProgramResult find_or_load(const ShaderMapProgramKey& key);

    private:
        struct ProgramKey
        {
            std::string shader_name;
            std::string pass_name;
            ShaderPlatform platform = ShaderPlatform::VulkanPortableV1;
            ShaderContentHash permutation_key{};

            bool operator==(const ProgramKey& other) const;
        };

        struct ProgramKeyHash
        {
            std::size_t operator()(const ProgramKey& key) const;
        };

        static ProgramKey make_key(const ShaderMapProgramData& program);
        static ProgramKey make_key(const ShaderMapProgramKey& key);

        ShaderMapLoader& loader_;
        std::unordered_map<ProgramKey, ShaderMapProgramRef, ProgramKeyHash> programs_;
    };
}
