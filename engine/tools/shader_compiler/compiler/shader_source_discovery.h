#pragma once

#include <string>
#include <vector>

#include "file_system/file_system.h"
#include "shader/shader_program_contract.h"

namespace toy3d::shader
{
    struct DiscoveredShaderSource
    {
        VirtualPath path;
        std::string name;
        std::vector<std::string> pass_names;
        ShaderUsage usage = ShaderUsage::Global;
        std::uint32_t vertex_factory_support = 0u;
        std::vector<ShaderPassRole> pass_roles;
        std::string error;
        bool name_conflict = false;
    };
    // A bounded declaration snapshot; name/Pass policy belongs to its consumer.
    // Discovery does not compile, publish Programs or own GPU/Editor state.
    // Invalid sources remain as path/error records; traversal failures abort the
    // snapshot. Pass names retain declaration order, records are sorted by path.
    FileResult<std::vector<DiscoveredShaderSource>> discover_shader_sources(const FileSystem& files,
                                                                            const VirtualPath& root);
} // namespace toy3d::shader
