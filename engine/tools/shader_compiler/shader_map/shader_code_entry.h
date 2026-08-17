#pragma once

#include "reflection/spirv_reflection.h"

#include <filesystem>
#include <optional>
#include <string>

namespace toy3d::shader
{
    constexpr std::uint32_t shader_code_entry_version = 1;

    struct ShaderCodeEntryWriteResult
    {
        std::optional<std::filesystem::path> entry_directory;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderCodeEntryWriteResult write_verified_shader_code_entry(
        const std::filesystem::path& entry_root,
        const ShaderCompileRequest& request,
        const TargetBindingLayout& target_layout,
        const ShaderStageReflection& reflection,
        const std::vector<std::uint8_t>& binary);

    std::string serialize_shader_stage_reflection(const ShaderStageReflection& reflection);
}
