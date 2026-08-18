#pragma once

#include "compiler/shader_compiler.h"
#include "compiler/variant_permutation.h"
#include "format/shader_format_types.h"
#include "frontend/shader_ast.h"

#include <optional>

namespace toy3d::shader
{
    struct ShaderProgramCompileInput
    {
        std::string pass_name;
        std::string source_virtual_path;
        ShaderDebugMode debug_mode = ShaderDebugMode::Development;
        std::vector<ShaderVariantSelection> variant_selections;
        const ShaderSourceProvider* source_provider = nullptr;
    };

    struct ShaderMapEntryCompileResult
    {
        std::optional<ShaderMapEntry> entry;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderMapEntryCompileResult compile_vulkan_shader_map_entry(
        const ShaderAsset& asset,
        const ShaderProgramCompileInput& input,
        const DiscoveredShaderToolchain& toolchain,
        PlatformFile& platform_file,
        const PhysicalPath& working_directory,
        const ShaderProcessRunner& process_runner = run_process);
}
