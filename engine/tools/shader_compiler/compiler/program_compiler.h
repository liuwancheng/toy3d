#pragma once

#include "compiler/shader_compiler.h"
#include "compiler/variant_permutation.h"
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

    struct ShaderMapBinding
    {
        ShaderParameterId binding_id = 0;
        std::string name;
        BindingGroup group = BindingGroup::Material;
        ShaderParameterCategory category = ShaderParameterCategory::Constant;
        ShaderStageFlags stages = ShaderStageFlags::None;
        NativeRegisterClass register_class = NativeRegisterClass::ConstantBuffer;
        std::uint32_t register_index = 0;
        std::uint32_t descriptor_set = 0;
        std::uint32_t descriptor_binding = 0;
    };

    struct ShaderCodeEntry
    {
        ShaderCompileRequest request;
        ShaderStageReflection reflection;
        std::vector<std::uint8_t> binary;
    };

    struct ShaderMapEntry
    {
        std::string shader_name;
        std::string pass_name;
        ShaderTarget target = ShaderTarget::VulkanSpirV;
        ShaderCompileProfile profile = ShaderCompileProfile::VulkanPortableV1;
        Sha256Hash logical_layout_hash{};
        Sha256Hash target_binding_hash{};
        Sha256Hash pass_template_hash{};
        std::uint32_t variant_id_version = shader_variant_id_version;
        std::uint32_t permutation_version = shader_permutation_version;
        Sha256Hash permutation_key{};
        std::uint32_t mapping_version = 0;
        std::vector<ShaderMapBinding> bindings;
        std::vector<ShaderCodeEntry> stages;
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
