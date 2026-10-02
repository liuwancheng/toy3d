#pragma once

#include "rendercore/shader/shader_map_program.h"
#include "shader/shader_map_index.h"

#include <optional>
#include <string>

namespace toy3d
{
    struct ShaderMapProgramLoadResult
    {
        // optional publishes program data only after loading and validation
        // succeed, so failure cannot expose a partially initialized program.
        std::optional<ShaderMapProgramData> program;
        std::string error;

        bool succeeded() const;
    };

    struct ShaderMapCollectionLoadResult
    {
        shader::ShaderMapIndex index;
        std::vector<ShaderMapProgramData> programs;
        std::string error;

        bool succeeded() const;
    };

    class ShaderMapLoader
    {
      public:
        virtual ~ShaderMapLoader() = default;
        virtual ShaderMapProgramLoadResult load_program(const ShaderMapProgramKey& key) const = 0;
        // A loader may implement individual programs only. Collection loading
        // fails explicitly unless it can validate a complete published index.
        virtual ShaderMapCollectionLoadResult load_collection(const std::string& shader_name, ShaderPlatform platform,
                                                              const ShaderContentHash& permutation_key) const;
    };

    ShaderMapProgramLoadResult validate_shader_map_program(ShaderMapProgramData program,
                                                           const ShaderMapProgramKey& key);
} // namespace toy3d
