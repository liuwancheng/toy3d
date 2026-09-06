#include "compiler/shader_compiler.h"

#include <utility>

namespace toy3d::shader
{
    bool VulkanShaderCodeEntryResult::succeeded() const
    {
        return entry_directory.has_value() && diagnostics.empty();
    }

    VulkanShaderCodeEntryResult compile_vulkan_shader_code_entry(
        const ShaderCompileRequest& request, const TargetBindingLayout& target_layout,
        const DiscoveredShaderToolchain& toolchain, PlatformFile& platform_file, const PhysicalPath& working_directory,
        const PhysicalPath& entry_root, const ShaderProcessRunner& process_runner)
    {
        VulkanShaderCodeEntryResult result;
        ShaderCompilerOutput compiled =
            compile_vulkan_shader(request, toolchain, platform_file, working_directory, process_runner);
        if (!compiled.succeeded())
        {
            result.diagnostics = std::move(compiled.diagnostics);
            return result;
        }
        SpirvReflectionResult reflected = reflect_and_validate_spirv(*compiled.binary, request, target_layout);
        if (!reflected.succeeded())
        {
            result.diagnostics = std::move(reflected.diagnostics);
            return result;
        }
        ShaderCodeEntryWriteResult written = write_verified_shader_code_entry(
            platform_file, entry_root, request, target_layout, *reflected.reflection, *compiled.binary);
        result.diagnostics = std::move(written.diagnostics);
        result.entry_directory = std::move(written.entry_directory);
        return result;
    }
} // namespace toy3d::shader
