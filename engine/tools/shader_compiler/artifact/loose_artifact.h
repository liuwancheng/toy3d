#pragma once

#include "reflection/spirv_reflection.h"

#include <filesystem>
#include <optional>

namespace toy3d::shader
{
    constexpr std::uint32_t shader_loose_artifact_version = 1;

    struct LooseArtifactWriteResult
    {
        std::optional<std::filesystem::path> artifact_directory;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    LooseArtifactWriteResult write_verified_loose_artifact(
        const std::filesystem::path& artifact_root,
        const ShaderCompileRequest& request,
        const TargetBindingLayout& target_layout,
        const ShaderStageReflection& reflection,
        const std::vector<std::uint8_t>& binary);
}
