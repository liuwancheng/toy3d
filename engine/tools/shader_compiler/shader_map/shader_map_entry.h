#pragma once

#include "format/shader_map_entry.h"
#include "frontend/diagnostic.h"

#include <optional>

namespace toy3d::shader
{
    struct ShaderMapEntryWriteResult
    {
        // optional reports a directory only for a fully written ShaderMap entry;
        // diagnostics describe failures before publication.
        std::optional<PhysicalPath> entry_directory;
        Sha256Hash shader_map_key{};
        Sha256Hash entry_content_hash{};
        bool cache_hit = false;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderMapEntryWriteResult write_verified_shader_map_entry(
        PlatformFile& platform_file,
        const PhysicalPath& shader_map_root,
        const ShaderMapEntry& entry);
}
