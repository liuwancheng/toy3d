#pragma once

#include "compiler/program_compiler.h"

#include <optional>

namespace toy3d::shader
{
    constexpr std::uint32_t shader_map_entry_version = 1;

    struct ShaderMapEntryWriteResult
    {
        std::optional<PhysicalPath> entry_directory;
        Sha256Hash shader_map_key{};
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderMapEntryWriteResult write_verified_shader_map_entry(
        PlatformFile& platform_file,
        const PhysicalPath& shader_map_root,
        const ShaderMapEntry& entry);
}
