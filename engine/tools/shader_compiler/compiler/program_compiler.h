#pragma once

#include "compiler/shader_compiler.h"
#include "frontend/shader_ast.h"

#include <optional>

namespace toy3d::shader
{
    struct ShaderProgramCompileInput
    {
        std::string pass_name;
        std::string source_virtual_path;
        ShaderDebugMode debug_mode = ShaderDebugMode::Development;
        std::string generated_prelude;
        std::vector<VirtualIncludeFile> include_files;
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
        const std::filesystem::path& working_directory,
        const ShaderProcessRunner& process_runner = run_process);
}
