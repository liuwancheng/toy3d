#pragma once

#include "compiler/compile_request.h"
#include "compiler/process_runner.h"
#include "compiler/toolchain_manifest.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace toy3d::shader
{
    using ShaderProcessRunner = std::function<ProcessResult(
        const std::filesystem::path&,
        const std::vector<std::string>&)>;

    struct DxcInvocation
    {
        std::vector<std::string> arguments;
    };

    struct VulkanCompileResult
    {
        std::optional<std::vector<std::uint8_t>> binary;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    std::optional<DxcInvocation> build_vulkan_dxc_invocation(
        const ShaderCompileRequest& request,
        const std::filesystem::path& source_path,
        const std::filesystem::path& output_path,
        std::vector<Diagnostic>& diagnostics);

    VulkanCompileResult compile_vulkan_shader(
        const ShaderCompileRequest& request,
        const DiscoveredShaderToolchain& toolchain,
        const std::filesystem::path& working_directory,
        const ShaderProcessRunner& process_runner = run_process);
}
