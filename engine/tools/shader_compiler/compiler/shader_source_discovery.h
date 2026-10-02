#pragma once

#include <string>
#include <vector>

#include "file_system/file_system.h"

namespace toy3d::shader
{
    struct DiscoveredShaderSource
    {
        VirtualPath path;
        std::string name;
        std::vector<std::string> pass_names;
        std::string error;
        bool name_conflict = false;
    };
    // A bounded declaration snapshot; name/Pass policy belongs to its consumer.
    // Discovery does not compile, publish Programs or own GPU/Editor state.
    // Invalid sources remain as path/error records; traversal failures abort the
    // snapshot. Pass names retain declaration order, records are sorted by path.
    FileResult<std::vector<DiscoveredShaderSource>> discover_shader_sources(
        const FileSystem& files, const VirtualPath& root);
}
