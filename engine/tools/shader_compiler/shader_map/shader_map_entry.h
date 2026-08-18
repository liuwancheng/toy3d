#pragma once

#include "compiler/program_compiler.h"

#include <optional>

namespace toy3d::shader
{
    constexpr std::uint32_t shader_map_entry_version = 2;

    struct ShaderMapEntryWriteResult
    {
        std::optional<PhysicalPath> entry_directory;
        Sha256Hash shader_map_key{};
        Sha256Hash entry_content_hash{};
        bool cache_hit = false;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    struct ShaderMapEntryReadResult
    {
        std::optional<ShaderMapEntry> entry;
        std::optional<PhysicalPath> entry_directory;
        Sha256Hash shader_map_key{};
        Sha256Hash entry_content_hash{};
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    Sha256Hash calculate_shader_map_key(const ShaderMapEntry& entry);
    Sha256Hash calculate_shader_map_entry_content_hash(const ShaderMapEntry& entry);

    ShaderMapEntryReadResult read_verified_shader_map_entry(
        const PlatformFile& platform_file,
        const PhysicalPath& shader_map_root,
        const Sha256Hash& shader_map_key);

    ShaderMapEntryWriteResult write_verified_shader_map_entry(
        PlatformFile& platform_file,
        const PhysicalPath& shader_map_root,
        const ShaderMapEntry& entry);
}
