#pragma once

#include "compiler/compile_request.h"
#include "platform/platform_services.h"
#include "compiler/toolchain_manifest.h"

#include <functional>
#include <optional>
#include <vector>

namespace toy3d::shader
{
    using ShaderProcessRunner = std::function<ProcessResult(const PhysicalPath&, const std::vector<std::string>&)>;

    struct DxcInvocation
    {
        std::vector<std::string> arguments;
    };

    struct ShaderCompilerOutput
    {
        // optional distinguishes a produced binary from compile failure without
        // treating an empty byte vector as an error sentinel.
        std::optional<std::vector<std::uint8_t>> binary;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    // optional rejects invalid profiles before an incomplete DXC process
    // invocation can be launched.
    std::optional<DxcInvocation> build_vulkan_dxc_invocation(const ShaderCompileRequest& request,
                                                             const PhysicalPath& source_path,
                                                             const PhysicalPath& output_path,
                                                             std::vector<Diagnostic>& diagnostics);

    // working_directory belongs exclusively to this invocation until it returns;
    // concurrent callers must supply different request/stage directories.
    ShaderCompilerOutput compile_vulkan_shader(const ShaderCompileRequest& request,
                                               const DiscoveredShaderToolchain& toolchain, PlatformFile& platform_file,
                                               const PhysicalPath& working_directory,
                                               const ShaderProcessRunner& process_runner = {});
} // namespace toy3d::shader
