#pragma once

#include "shader/shader_format_types.h"
#include "compiler/shader_source_provider.h"
#include "frontend/diagnostic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    constexpr std::uint32_t default_max_include_depth = 64;

    struct IncludeResolveResult
    {
        // optional publishes expanded source only when the full recursive
        // include graph resolves successfully.
        std::optional<std::string> source;
        std::vector<ShaderDependency> dependencies;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    IncludeResolveResult resolve_shader_includes(const std::string& source, const std::string& source_virtual_path,
                                                 const ShaderSourceProvider& source_provider,
                                                 std::uint32_t max_depth = default_max_include_depth);
} // namespace toy3d::shader
