#include "compiler/dxc_adapter.h"

namespace toy3d::shader
{
    // DXC setup and output use optional so invalid invocations and failed
    // compiles cannot expose partially usable command lines or binaries.
    namespace
    {
        const char* profile_name(ShaderStageFlags stage)
        {
            switch (stage)
            {
            case ShaderStageFlags::Vertex:
                return "vs_6_0";
            case ShaderStageFlags::Pixel:
                return "ps_6_0";
            case ShaderStageFlags::Compute:
                return "cs_6_0";
            default:
                return nullptr;
            }
        }

        void add_error(std::vector<Diagnostic>& diagnostics, DiagnosticCode code, const ShaderCompileRequest& request,
                       const std::string& message)
        {
            diagnostics.push_back({DiagnosticSeverity::Error,
                                   code,
                                   {request.source_virtual_path, 0, 1, 1},
                                   message + " [stage=" + std::to_string(static_cast<std::uint32_t>(request.stage)) +
                                       ", entry=" + request.entry_point + ", target=VulkanES31]"});
        }

        bool is_spirv_1_3_or_older(const std::vector<std::uint8_t>& binary)
        {
            if (binary.size() < 20u || binary.size() % 4u != 0u)
            {
                return false;
            }
            const std::uint32_t magic =
                static_cast<std::uint32_t>(binary[0]) | (static_cast<std::uint32_t>(binary[1]) << 8u) |
                (static_cast<std::uint32_t>(binary[2]) << 16u) | (static_cast<std::uint32_t>(binary[3]) << 24u);
            const std::uint32_t version =
                static_cast<std::uint32_t>(binary[4]) | (static_cast<std::uint32_t>(binary[5]) << 8u) |
                (static_cast<std::uint32_t>(binary[6]) << 16u) | (static_cast<std::uint32_t>(binary[7]) << 24u);
            return magic == 0x07230203u && version <= 0x00010300u;
        }
    } // namespace

    bool ShaderCompilerOutput::succeeded() const
    {
        return binary.has_value() && diagnostics.empty();
    }

    std::optional<DxcInvocation> build_vulkan_dxc_invocation(const ShaderCompileRequest& request,
                                                             const PhysicalPath& source_path,
                                                             const PhysicalPath& output_path,
                                                             std::vector<Diagnostic>& diagnostics)
    {
        if (request.target != ShaderTarget::VulkanSpirV || request.profile != ShaderCompileProfile::VulkanES31 ||
            profile_name(request.stage) == nullptr)
        {
            add_error(diagnostics, DiagnosticCode::InvalidCompileRequest, request,
                      "DXC Vulkan adapter received an incompatible compile request.");
            return std::nullopt;
        }
        DxcInvocation invocation;
        invocation.arguments = {"-spirv",
                                "-fspv-target-env=vulkan1.1",
                                "-fvk-use-dx-layout",
                                "-Zpc",
                                "-E",
                                request.entry_point,
                                "-T",
                                profile_name(request.stage),
                                "-Fo",
                                output_path.utf8()};
        switch (request.debug_mode)
        {
        case ShaderDebugMode::Debug:
            invocation.arguments.insert(invocation.arguments.end(),
                                        {"-Od", "-Zi", "-Qembed_debug", "-fspv-debug=vulkan-with-source"});
            break;
        case ShaderDebugMode::Development:
            invocation.arguments.insert(invocation.arguments.end(), {"-O3", "-Zi", "-fspv-debug=line"});
            break;
        case ShaderDebugMode::Shipping:
            invocation.arguments.insert(invocation.arguments.end(), {"-O3", "-Qstrip_debug", "-Qstrip_reflect"});
            break;
        }
        invocation.arguments.push_back(source_path.utf8());
        return invocation;
    }

    ShaderCompilerOutput compile_vulkan_shader(const ShaderCompileRequest& request,
                                               const DiscoveredShaderToolchain& toolchain, PlatformFile& platform_file,
                                               const PhysicalPath& working_directory,
                                               const ShaderProcessRunner& process_runner)
    {
        ShaderCompilerOutput result;
        if (request.compiler_identity != toolchain.manifest.identity)
        {
            add_error(result.diagnostics, DiagnosticCode::CompilerUnavailable, request,
                      "Compile request compiler identity does not match the discovered locked toolchain.");
            return result;
        }
        const FileStatus created = platform_file.create_directories(working_directory);
        if (!created.succeeded())
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                      "Failed to create Shader compiler working directory: " + created.message);
            return result;
        }
        // Each caller supplies an exclusive request/stage directory. The full
        // compile identity lives in the artifact metadata; repeating its hash
        // in temporary filenames can exceed external tools' Windows path limit.
        const FileResult<PhysicalPath> source_path = platform_file.join_relative(working_directory, "input.hlsl");
        const FileResult<PhysicalPath> output_path = platform_file.join_relative(working_directory, "output.spv");
        if (!source_path.succeeded() || !output_path.succeeded())
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                      "Failed to resolve Shader compiler temporary paths.");
            return result;
        }
        const FileResult<bool> output_exists = platform_file.exists(output_path.value());
        if (!output_exists.succeeded())
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                      "Failed to inspect stale Shader compiler output: " + output_exists.status().message);
            return result;
        }
        if (output_exists.value())
        {
            const FileStatus removed = platform_file.remove_file(output_path.value());
            if (!removed.succeeded())
            {
                add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                          "Failed to remove a stale Shader compiler output: " + removed.message);
                return result;
            }
        }
        const FileStatus source_written =
            platform_file.write_text_utf8(source_path.value(), request.source, FileWriteMode::Truncate);
        if (!source_written.succeeded())
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                      "Failed to write generated Shader source: " + source_written.message);
            return result;
        }
        std::vector<Diagnostic> invocation_diagnostics;
        const auto invocation =
            build_vulkan_dxc_invocation(request, source_path.value(), output_path.value(), invocation_diagnostics);
        if (!invocation)
        {
            result.diagnostics = std::move(invocation_diagnostics);
            return result;
        }
        const NativeProcessService native_process;
        const ProcessResult compiled = process_runner ? process_runner(toolchain.dxc_path, invocation->arguments)
                                                      : native_process.run(toolchain.dxc_path, invocation->arguments);
        if (!compiled.succeeded())
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                      "DXC failed (exit " + std::to_string(compiled.exit_code) + "): " + compiled.message + "\n" +
                          compiled.output);
            return result;
        }
        FileResult<std::vector<std::uint8_t>> binary = platform_file.read_binary(output_path.value());
        if (!binary.succeeded() || !is_spirv_1_3_or_older(binary.value()))
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                      "DXC did not produce a valid SPIR-V 1.3-or-older module.");
            return result;
        }
        const ProcessResult validated =
            process_runner
                ? process_runner(toolchain.spirv_val_path, {"--target-env", "vulkan1.1", output_path.value().utf8()})
                : native_process.run(toolchain.spirv_val_path,
                                     {"--target-env", "vulkan1.1", output_path.value().utf8()});
        if (!validated.succeeded())
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderValidationFailed, request,
                      "spirv-val failed (exit " + std::to_string(validated.exit_code) + "): " + validated.message +
                          "\n" + validated.output);
            return result;
        }
        result.binary = std::move(binary.value());
        return result;
    }
} // namespace toy3d::shader
