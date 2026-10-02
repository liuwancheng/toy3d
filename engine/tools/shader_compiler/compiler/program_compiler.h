#pragma once

#include "compiler/shader_compiler.h"
#include "compiler/variant_permutation.h"
#include "shader/shader_format_types.h"
#include "frontend/shader_ast.h"
#include "shader/shader_editor_properties.h"

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
        // optional keeps a failed multi-stage compile from exposing an entry
        // assembled from only a subset of its stages.
        std::optional<ShaderMapEntry> entry;
        std::vector<Diagnostic> diagnostics;
        std::vector<ShaderEditorProperty> editor_properties;

        bool succeeded() const;
    };

    ShaderMapEntryCompileResult compile_vulkan_shader_map_entry(const ShaderAsset& asset,
                                                                const ShaderProgramCompileInput& input,
                                                                const DiscoveredShaderToolchain& toolchain,
                                                                PlatformFile& platform_file,
                                                                const PhysicalPath& working_directory,
                                                                const ShaderProcessRunner& process_runner = {});
} // namespace toy3d::shader
