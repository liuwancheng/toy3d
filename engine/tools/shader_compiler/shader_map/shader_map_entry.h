#pragma once

#include "compiler/program_compiler.h"

#include <filesystem>
#include <optional>

namespace toy3d::shader
{
    constexpr std::uint32_t shader_map_entry_version = 1;

    struct ShaderMapEntryWriteResult
    {
        std::optional<std::filesystem::path> entry_directory;
        Sha256Hash shader_map_key{};
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderMapEntryWriteResult write_verified_shader_map_entry(
        const std::filesystem::path& shader_map_root,
        const ShaderMapEntry& entry);
}
