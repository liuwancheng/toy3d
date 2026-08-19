#pragma once

#include "rendercore/shader/shader_map_program.h"

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

    class ShaderMapLoader
    {
    public:
        virtual ~ShaderMapLoader() = default;
        virtual ShaderMapProgramLoadResult load_program(
            const ShaderMapProgramKey& key) const = 0;
    };

    ShaderMapProgramLoadResult validate_shader_map_program(
        ShaderMapProgramData program,
        const ShaderMapProgramKey& key);
}
