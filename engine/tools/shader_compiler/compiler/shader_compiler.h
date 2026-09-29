#pragma once

#include "shader_map/shader_code_entry.h"
#include "compiler/dxc_adapter.h"

namespace toy3d::shader
{
    struct VulkanShaderCodeEntryResult
    {
        // optional reports the entry directory only after publication succeeds,
        // so callers cannot consume incomplete output.
        std::optional<PhysicalPath> entry_directory;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    VulkanShaderCodeEntryResult compile_vulkan_shader_code_entry(
        const ShaderCompileRequest& request, const TargetBindingLayout& target_layout,
        const DiscoveredShaderToolchain& toolchain, PlatformFile& platform_file, const PhysicalPath& working_directory,
        const PhysicalPath& entry_root, const ShaderProcessRunner& process_runner = {});
} // namespace toy3d::shader
