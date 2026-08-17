#pragma once

#include "shader_map/shader_code_entry.h"
#include "compiler/dxc_adapter.h"

namespace toy3d::shader
{
    struct VulkanShaderCodeEntryResult
    {
        std::optional<std::filesystem::path> entry_directory;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    VulkanShaderCodeEntryResult compile_vulkan_shader_code_entry(
        const ShaderCompileRequest& request,
        const TargetBindingLayout& target_layout,
        const DiscoveredShaderToolchain& toolchain,
        const std::filesystem::path& working_directory,
        const std::filesystem::path& entry_root,
        const ShaderProcessRunner& process_runner = run_process);
}
