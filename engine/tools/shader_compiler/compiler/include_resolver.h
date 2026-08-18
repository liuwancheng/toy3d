#pragma once

#include "common/sha256.h"
#include "compiler/shader_source_provider.h"
#include "frontend/diagnostic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    constexpr std::uint32_t default_max_include_depth = 64;

    struct ShaderDependency
    {
        std::string virtual_path;
        Sha256Hash content_hash{};
    };

    struct IncludeResolveResult
    {
        std::optional<std::string> source;
        std::vector<ShaderDependency> dependencies;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    IncludeResolveResult resolve_shader_includes(
        const std::string& source,
        const std::string& source_virtual_path,
        const ShaderSourceProvider& source_provider,
        std::uint32_t max_depth = default_max_include_depth);
}
