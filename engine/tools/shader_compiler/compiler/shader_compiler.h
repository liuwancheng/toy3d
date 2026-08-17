#pragma once

#include "artifact/loose_artifact.h"
#include "compiler/dxc_adapter.h"

namespace toy3d::shader
{
    struct VulkanArtifactCompileResult
    {
        std::optional<std::filesystem::path> artifact_directory;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    VulkanArtifactCompileResult compile_vulkan_loose_artifact(
        const ShaderCompileRequest& request,
        const TargetBindingLayout& target_layout,
        const DiscoveredShaderToolchain& toolchain,
        const std::filesystem::path& working_directory,
        const std::filesystem::path& artifact_root,
        const ShaderProcessRunner& process_runner = run_process);
}
