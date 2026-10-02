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
        ShaderMapProgram(const ShaderMapProgram&) = delete;
        ShaderMapProgram& operator=(const ShaderMapProgram&) = delete;
        // ShaderMap moves validated program data into make_shared; the data
        // constructor stays private so no caller can publish an unchecked program.
        ShaderMapProgram(ShaderMapProgram&&) noexcept = default;
        ShaderMapProgram& operator=(ShaderMapProgram&&) noexcept = default;

        const ShaderMapProgramData& data() const;
        const std::shared_ptr<const ShaderMapProgram>& gpu_skin_program() const;

        const ShaderParameterBinding* find_parameter_binding(ShaderParameterId parameter_id) const;

      private:
        friend class ShaderMap;

        explicit ShaderMapProgram(ShaderMapProgramData data);

        ShaderMapProgramData data_;
        std::shared_ptr<const ShaderMapProgram> gpu_skin_program_;

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
        // Build an immutable, verified revision without mutating a cached key.
        // The caller publishes it only after its material/pipeline checks pass.
        static ShaderMapProgramResult create_candidate(ShaderMapProgramData data, const ShaderMapProgramKey& key);

      private:
        struct ProgramKey
        {
            std::string shader_name;
            std::string pass_name;
            ShaderPlatform platform = ShaderPlatform::VulkanES31;
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
} // namespace toy3d
