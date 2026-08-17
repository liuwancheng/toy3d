#include "compiler/shader_compiler.h"

#include <utility>

namespace toy3d::shader
{
    bool VulkanArtifactCompileResult::succeeded() const
    {
        return artifact_directory.has_value() && diagnostics.empty();
    }

    VulkanArtifactCompileResult compile_vulkan_loose_artifact(
        const ShaderCompileRequest& request,
        const TargetBindingLayout& target_layout,
        const DiscoveredShaderToolchain& toolchain,
        const std::filesystem::path& working_directory,
        const std::filesystem::path& artifact_root,
        const ShaderProcessRunner& process_runner)
    {
        VulkanArtifactCompileResult result;
        VulkanCompileResult compiled = compile_vulkan_shader(
            request, toolchain, working_directory, process_runner);
        if (!compiled.succeeded())
        {
            result.diagnostics = std::move(compiled.diagnostics);
            return result;
        }
        SpirvReflectionResult reflected = reflect_and_validate_spirv(
            *compiled.binary, request, target_layout);
        if (!reflected.succeeded())
        {
            result.diagnostics = std::move(reflected.diagnostics);
            return result;
        }
        LooseArtifactWriteResult written = write_verified_loose_artifact(
            artifact_root, request, target_layout, *reflected.reflection, *compiled.binary);
        result.diagnostics = std::move(written.diagnostics);
        result.artifact_directory = std::move(written.artifact_directory);
        return result;
    }
}
