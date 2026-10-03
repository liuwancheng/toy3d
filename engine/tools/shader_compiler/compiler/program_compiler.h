#pragma once

#include "compiler/shader_compiler.h"
#include "compiler/variant_permutation.h"
#include "shader/shader_format_types.h"
#include "frontend/shader_ast.h"
#include "shader/shader_editor_properties.h"

#include <optional>
#include <map>
#include <array>

namespace toy3d::shader
{
    // Owned by one source job. Cache only binaries already validated by the compiler;
    // every reuse still reflects against the current Program mapping.
    struct ShaderStageCompileCache
    {
        std::map<Sha256Hash, std::vector<std::uint8_t>> binaries;
        std::array<std::size_t, 3> compiled{};
        std::array<std::size_t, 3> reused{};
    };

    struct ShaderProgramCompileInput
    {
        std::string pass_name;
        std::string source_virtual_path;
        ShaderDebugMode debug_mode = ShaderDebugMode::Development;
        std::vector<ShaderVariantSelection> variant_selections;
        std::vector<ShaderPermutationSelection> pass_selections;
        ShaderCompilePolicy compile_policy;
        VertexFactoryType vertex_factory = VertexFactoryType::None;
        const ShaderSourceProvider* source_provider = nullptr;
        ShaderStageCompileCache* stage_cache = nullptr;
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

    bool supports_gpu_skin(const ShaderAsset& asset, const std::string& pass_name);

    ShaderMapEntryCompileResult compile_vulkan_shader_map_entry(const ShaderAsset& asset,
                                                                const ShaderProgramCompileInput& input,
                                                                const DiscoveredShaderToolchain& toolchain,
                                                                PlatformFile& platform_file,
                                                                const PhysicalPath& working_directory,
                                                                const ShaderProcessRunner& process_runner = {});
} // namespace toy3d::shader
