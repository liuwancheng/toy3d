#pragma once

#include "compiler/include_resolver.h"
#include "format/shader_format_types.h"
#include "layout/binding_allocator.h"

#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    struct ShaderCompileRequestInput
    {
        ShaderTarget target = ShaderTarget::VulkanSpirV;
        ShaderCompileProfile profile = ShaderCompileProfile::VulkanPortableV1;
        ShaderStageFlags stage = ShaderStageFlags::None;
        ShaderDebugMode debug_mode = ShaderDebugMode::Development;
        std::string entry_point;
        std::string source_virtual_path;
        std::string compiler_identity;
        std::string generated_prelude;
        std::string generated_bindings;
        std::string shader_include_source;
        std::string pass_source;
        const ShaderSourceProvider* source_provider = nullptr;
        Sha256Hash logical_layout_hash{};
        Sha256Hash target_binding_hash{};
    };

    struct ShaderCompileRequestResult
    {
        std::optional<ShaderCompileRequest> request;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderCompileRequestResult build_shader_compile_request(const ShaderCompileRequestInput& input);
}
