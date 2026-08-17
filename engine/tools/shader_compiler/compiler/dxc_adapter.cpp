#include "compiler/dxc_adapter.h"

#include <fstream>

namespace toy3d::shader
{
    namespace
    {
        const char* profile_name(ShaderStageFlags stage)
        {
            switch (stage)
            {
            case ShaderStageFlags::Vertex: return "vs_6_0";
            case ShaderStageFlags::Pixel: return "ps_6_0";
            case ShaderStageFlags::Compute: return "cs_6_0";
            default: return nullptr;
            }
        }

        void add_error(
            std::vector<Diagnostic>& diagnostics,
            DiagnosticCode code,
            const ShaderCompileRequest& request,
            const std::string& message)
        {
            diagnostics.push_back({DiagnosticSeverity::Error, code,
                {request.source_virtual_path, 0, 1, 1},
                message + " [stage=" + std::to_string(static_cast<std::uint32_t>(request.stage)) +
                    ", entry=" + request.entry_point + ", target=VulkanPortableV1]"});
        }

        bool write_source(const std::filesystem::path& path, const std::string& source)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            return output && static_cast<bool>(output.write(source.data(), static_cast<std::streamsize>(source.size())));
        }

        bool read_binary(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) return false;
            const std::streamoff size = input.tellg();
            if (size < 0) return false;
            bytes.resize(static_cast<std::size_t>(size));
            input.seekg(0, std::ios::beg);
            return bytes.empty() || static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()), size));
        }

        bool is_spirv_1_3_or_older(const std::vector<std::uint8_t>& binary)
        {
            if (binary.size() < 20u || binary.size() % 4u != 0u) return false;
            const std::uint32_t magic = static_cast<std::uint32_t>(binary[0]) |
                (static_cast<std::uint32_t>(binary[1]) << 8u) |
                (static_cast<std::uint32_t>(binary[2]) << 16u) |
                (static_cast<std::uint32_t>(binary[3]) << 24u);
            const std::uint32_t version = static_cast<std::uint32_t>(binary[4]) |
                (static_cast<std::uint32_t>(binary[5]) << 8u) |
                (static_cast<std::uint32_t>(binary[6]) << 16u) |
                (static_cast<std::uint32_t>(binary[7]) << 24u);
            return magic == 0x07230203u && version <= 0x00010300u;
        }
    }

    bool ShaderCompilerOutput::succeeded() const
    {
        return binary.has_value() && diagnostics.empty();
    }

    std::optional<DxcInvocation> build_vulkan_dxc_invocation(
        const ShaderCompileRequest& request,
        const std::filesystem::path& source_path,
        const std::filesystem::path& output_path,
        std::vector<Diagnostic>& diagnostics)
    {
        if (request.target != ShaderTarget::VulkanSpirV ||
            request.profile != ShaderCompileProfile::VulkanPortableV1 ||
            profile_name(request.stage) == nullptr)
        {
            add_error(diagnostics, DiagnosticCode::InvalidCompileRequest, request,
                "DXC Vulkan adapter received an incompatible compile request.");
            return std::nullopt;
        }
        DxcInvocation invocation;
        invocation.arguments = {
            "-spirv",
            "-fspv-target-env=vulkan1.1",
            "-fvk-use-dx-layout",
            "-Zpc",
            "-E", request.entry_point,
            "-T", profile_name(request.stage),
            "-Fo", output_path.generic_string()};
        switch (request.debug_mode)
        {
        case ShaderDebugMode::Debug:
            invocation.arguments.insert(invocation.arguments.end(), {"-Od", "-Zi", "-Qembed_debug", "-fspv-debug=vulkan-with-source"});
            break;
        case ShaderDebugMode::Development:
            invocation.arguments.insert(invocation.arguments.end(), {"-O3", "-Zi", "-fspv-debug=line"});
            break;
        case ShaderDebugMode::Shipping:
            invocation.arguments.insert(invocation.arguments.end(), {"-O3", "-Qstrip_debug", "-Qstrip_reflect"});
            break;
        }
        invocation.arguments.push_back(source_path.generic_string());
        return invocation;
    }

    ShaderCompilerOutput compile_vulkan_shader(
        const ShaderCompileRequest& request,
        const DiscoveredShaderToolchain& toolchain,
        const std::filesystem::path& working_directory,
        const ShaderProcessRunner& process_runner)
    {
        ShaderCompilerOutput result;
        if (request.compiler_identity != toolchain.manifest.identity)
        {
            add_error(result.diagnostics, DiagnosticCode::CompilerUnavailable, request,
                "Compile request compiler identity does not match the discovered locked toolchain.");
            return result;
        }
        std::error_code error;
        std::filesystem::create_directories(working_directory, error);
        if (error)
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                "Failed to create Shader compiler working directory: " + error.message());
            return result;
        }
        const std::string key = sha256_to_hex(request.compile_key);
        const std::filesystem::path source_path = working_directory / (key + ".hlsl");
        const std::filesystem::path output_path = working_directory / (key + ".spv");
        std::filesystem::remove(output_path, error);
        if (error)
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                "Failed to remove a stale Shader compiler output: " + error.message());
            return result;
        }
        if (!write_source(source_path, request.source))
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                "Failed to write generated Shader source.");
            return result;
        }
        std::vector<Diagnostic> invocation_diagnostics;
        const auto invocation = build_vulkan_dxc_invocation(request, source_path, output_path, invocation_diagnostics);
        if (!invocation)
        {
            result.diagnostics = std::move(invocation_diagnostics);
            return result;
        }
        const ProcessResult compiled = process_runner(toolchain.dxc_path, invocation->arguments);
        if (!compiled.launched || compiled.exit_code != 0)
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                "DXC failed (exit " + std::to_string(compiled.exit_code) + "):\n" + compiled.output);
            return result;
        }
        std::vector<std::uint8_t> binary;
        if (!read_binary(output_path, binary) || !is_spirv_1_3_or_older(binary))
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderCompilationFailed, request,
                "DXC did not produce a valid SPIR-V 1.3-or-older module.");
            return result;
        }
        const ProcessResult validated = process_runner(toolchain.spirv_val_path,
            {"--target-env", "vulkan1.1", output_path.generic_string()});
        if (!validated.launched || validated.exit_code != 0)
        {
            add_error(result.diagnostics, DiagnosticCode::ShaderValidationFailed, request,
                "spirv-val failed (exit " + std::to_string(validated.exit_code) + "):\n" + validated.output);
            return result;
        }
        result.binary = std::move(binary);
        return result;
    }
}
