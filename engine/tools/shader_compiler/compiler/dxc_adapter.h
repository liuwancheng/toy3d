#pragma once

#include "compiler/compile_request.h"
#include "compiler/process_runner.h"
#include "compiler/toolchain_manifest.h"

#include <functional>
#include <optional>
#include <vector>

namespace toy3d::shader
{
    using ShaderProcessRunner = std::function<ProcessResult(
        const PhysicalPath&,
        const std::vector<std::string>&)>;

    struct DxcInvocation
    {
        std::vector<std::string> arguments;
    };

    struct ShaderCompilerOutput
    {
        std::optional<std::vector<std::uint8_t>> binary;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    std::optional<DxcInvocation> build_vulkan_dxc_invocation(
        const ShaderCompileRequest& request,
        const PhysicalPath& source_path,
        const PhysicalPath& output_path,
        std::vector<Diagnostic>& diagnostics);

    ShaderCompilerOutput compile_vulkan_shader(
        const ShaderCompileRequest& request,
        const DiscoveredShaderToolchain& toolchain,
        PlatformFile& platform_file,
        const PhysicalPath& working_directory,
        const ShaderProcessRunner& process_runner = run_process);
}
