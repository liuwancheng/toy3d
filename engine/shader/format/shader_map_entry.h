#pragma once

#include "file_system/platform_file.h"
#include "format/shader_format_types.h"

#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    constexpr std::uint32_t shader_map_entry_version = 5;

    struct ShaderMapEntryReadResult
    {
        // Parsed values are present only after the complete entry is accepted;
        // optional prevents callers from observing partial reader output.
        std::optional<ShaderMapEntry> entry;
        std::optional<PhysicalPath> entry_directory;
        Sha256Hash shader_map_key{};
        Sha256Hash entry_content_hash{};
        std::vector<std::string> diagnostics;

        bool succeeded() const;
    };

    Sha256Hash calculate_shader_map_key(const ShaderMapEntry& entry);
    Sha256Hash calculate_shader_map_entry_content_hash(const ShaderMapEntry& entry);
    std::string serialize_shader_parameter_schema(const ShaderParameterSchema& schema);
    bool parse_shader_parameter_schema(const std::string& text, ShaderParameterSchema& schema, std::string& error);

    ShaderMapEntryReadResult read_verified_shader_map_entry(const PlatformFile& platform_file,
                                                            const PhysicalPath& shader_map_root,
                                                            const Sha256Hash& shader_map_key);
} // namespace toy3d::shader
