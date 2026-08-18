#include "compiler/toolchain_manifest.h"

#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace toy3d::shader
{
    namespace
    {
        constexpr const char* manifest_file_name = "Toy3dShaderToolchain.manifest";

        void add_error(std::vector<Diagnostic>& diagnostics, DiagnosticCode code, const std::string& message)
        {
            diagnostics.push_back({DiagnosticSeverity::Error, code, {}, message});
        }

        std::optional<std::unordered_map<std::string, std::string>> parse_manifest(
            const PlatformFile& platform_file,
            const PhysicalPath& path,
            std::vector<Diagnostic>& diagnostics)
        {
            const FileResult<std::string> manifest = platform_file.read_text_utf8(path);
            if (!manifest.succeeded())
            {
                add_error(diagnostics, DiagnosticCode::CompilerUnavailable,
                    "Locked Shader toolchain manifest is missing or unreadable: " + path.utf8() +
                        " (" + manifest.status().message + ")");
                return std::nullopt;
            }
            std::istringstream input(manifest.value());
            std::unordered_map<std::string, std::string> fields;
            std::string line;
            std::uint32_t line_number = 0;
            while (std::getline(input, line))
            {
                ++line_number;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line.front() == '#') continue;
                const std::size_t separator = line.find('=');
                if (separator == std::string::npos || separator == 0 || separator + 1 >= line.size())
                {
                    add_error(diagnostics, DiagnosticCode::InvalidToolchainManifest,
                        "Invalid toolchain manifest field at line " + std::to_string(line_number) + ".");
                    continue;
                }
                const std::string key = line.substr(0, separator);
                const std::string value = line.substr(separator + 1);
                if (!fields.emplace(key, value).second)
                {
                    add_error(diagnostics, DiagnosticCode::InvalidToolchainManifest,
                        "Duplicate toolchain manifest field '" + key + "'.");
                }
            }
            if (!diagnostics.empty()) return std::nullopt;
            return fields;
        }

        std::optional<std::string> required_field(
            const std::unordered_map<std::string, std::string>& fields,
            const std::string& key,
            std::vector<Diagnostic>& diagnostics)
        {
            const auto found = fields.find(key);
            if (found == fields.end() || found->second.empty())
            {
                add_error(diagnostics, DiagnosticCode::InvalidToolchainManifest,
                    "Toolchain manifest is missing required field '" + key + "'.");
                return std::nullopt;
            }
            return found->second;
        }

        bool load_artifact(
            const std::unordered_map<std::string, std::string>& fields,
            const std::string& prefix,
            ToolchainArtifact& artifact,
            std::vector<Diagnostic>& diagnostics)
        {
            const auto path = required_field(fields, prefix + ".path", diagnostics);
            const auto hash = required_field(fields, prefix + ".sha256", diagnostics);
            const auto revision = required_field(fields, prefix + ".source_revision", diagnostics);
            const auto parameters = required_field(fields, prefix + ".build_parameters", diagnostics);
            const auto license = required_field(fields, prefix + ".license", diagnostics);
            const auto url = required_field(fields, prefix + ".source_url", diagnostics);
            if (!path || !hash || !revision || !parameters || !license || !url) return false;
            const std::optional<Sha256Hash> parsed_hash = sha256_from_hex(*hash);
            if (!parsed_hash)
            {
                add_error(diagnostics, DiagnosticCode::InvalidToolchainManifest,
                    "Toolchain artifact '" + prefix + "' has an invalid SHA-256.");
                return false;
            }
            if (path->empty() || path->front() == '/' || path->front() == '\\' ||
                path->find('\\') != std::string::npos || path->find(':') != std::string::npos)
            {
                add_error(diagnostics, DiagnosticCode::InvalidToolchainManifest,
                    "Toolchain artifact '" + prefix + "' must use a relative bundle path.");
                return false;
            }
            std::size_t begin = 0;
            while (begin <= path->size())
            {
                const std::size_t end = path->find('/', begin);
                const std::string_view component(
                    path->data() + begin,
                    (end == std::string::npos ? path->size() : end) - begin);
                if (component.empty() || component == "." || component == "..")
                {
                    add_error(diagnostics, DiagnosticCode::InvalidToolchainManifest,
                        "Toolchain artifact '" + prefix + "' escapes the bundle root.");
                    return false;
                }
                if (end == std::string::npos) break;
                begin = end + 1;
            }
            artifact.relative_path = *path;
            artifact.content_hash = *parsed_hash;
            artifact.source_revision = *revision;
            artifact.build_parameters = *parameters;
            artifact.license = *license;
            artifact.source_url = *url;
            return true;
        }

        bool load_metadata(
            const std::unordered_map<std::string, std::string>& fields,
            const std::string& prefix,
            std::string& version,
            std::string& url,
            std::string& license,
            std::vector<Diagnostic>& diagnostics)
        {
            const auto parsed_version = required_field(fields, prefix + ".version", diagnostics);
            const auto parsed_url = required_field(fields, prefix + ".source_url", diagnostics);
            const auto parsed_license = required_field(fields, prefix + ".license", diagnostics);
            if (!parsed_version || !parsed_url || !parsed_license) return false;
            version = *parsed_version;
            url = *parsed_url;
            license = *parsed_license;
            return true;
        }

        bool verify_artifact(
            const PlatformFile& platform_file,
            const PhysicalPath& bundle_root,
            const ToolchainArtifact& artifact,
            const std::string& name,
            PhysicalPath& resolved_path,
            std::vector<Diagnostic>& diagnostics)
        {
            const FileResult<PhysicalPath> joined =
                platform_file.join_relative(bundle_root, artifact.relative_path);
            if (!joined.succeeded())
            {
                add_error(diagnostics, DiagnosticCode::InvalidToolchainManifest,
                    "Locked Shader toolchain artifact has an invalid bundle path for " + name + ".");
                return false;
            }
            resolved_path = joined.value();
            const FileResult<std::vector<std::uint8_t>> bytes =
                platform_file.read_binary(resolved_path);
            if (!bytes.succeeded())
            {
                add_error(diagnostics, DiagnosticCode::CompilerUnavailable,
                    "Locked Shader toolchain artifact is missing or unreadable: " +
                        resolved_path.utf8() + " (" + bytes.status().message + ")");
                return false;
            }
            if (sha256(bytes.value()) != artifact.content_hash)
            {
                add_error(diagnostics, DiagnosticCode::ToolchainHashMismatch,
                    "Locked Shader toolchain artifact hash does not match for " + name + ": " + resolved_path.utf8());
                return false;
            }
            return true;
        }
    }

    bool ToolchainDiscoveryResult::succeeded() const
    {
        return toolchain.has_value() && diagnostics.empty();
    }

    std::string shader_toolchain_host_platform()
    {
#if defined(_WIN32) && defined(_M_X64)
        return "windows-x64";
#elif defined(_WIN32) && defined(_M_ARM64)
        return "windows-arm64";
#elif defined(__APPLE__) && defined(__aarch64__)
        return "macos-arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
        return "macos-x64";
#elif defined(__linux__) && defined(__aarch64__)
        return "linux-arm64";
#elif defined(__linux__) && defined(__x86_64__)
        return "linux-x64";
#else
        return "unknown";
#endif
    }

    FileResult<PhysicalPath> shader_toolchain_root_for_executable(
        const PlatformFile& platform_file,
        const PhysicalPath& executable_path)
    {
        const FileResult<PhysicalPath> parent = platform_file.parent_path(executable_path);
        if (!parent.succeeded()) return FileResult<PhysicalPath>(parent.status());
        const FileResult<PhysicalPath> toolchain =
            platform_file.join_relative(parent.value(), "ShaderToolchain");
        if (!toolchain.succeeded()) return FileResult<PhysicalPath>(toolchain.status());
        return platform_file.join_relative(toolchain.value(), shader_toolchain_host_platform());
    }

    ToolchainDiscoveryResult discover_shader_toolchain(
        const PlatformFile& platform_file,
        const PhysicalPath& explicit_bundle_root)
    {
        ToolchainDiscoveryResult result;
        if (explicit_bundle_root.empty())
        {
            add_error(result.diagnostics, DiagnosticCode::CompilerUnavailable,
                "An explicit Toy3dShaderToolchain bundle root is required.");
            return result;
        }
        const FileResult<PhysicalPath> manifest_path =
            platform_file.join_relative(explicit_bundle_root, manifest_file_name);
        if (!manifest_path.succeeded())
        {
            add_error(result.diagnostics, DiagnosticCode::CompilerUnavailable,
                "Unable to resolve the locked Shader toolchain manifest path.");
            return result;
        }
        const auto fields = parse_manifest(platform_file, manifest_path.value(), result.diagnostics);
        if (!fields) return result;

        ShaderToolchainManifest manifest;
        const auto version = required_field(*fields, "manifest_version", result.diagnostics);
        const auto identity = required_field(*fields, "bundle_identity", result.diagnostics);
        const auto platform = required_field(*fields, "host_platform", result.diagnostics);
        if (version != std::optional<std::string>("1"))
        {
            add_error(result.diagnostics, DiagnosticCode::InvalidToolchainManifest,
                "Unsupported Toy3dShaderToolchain manifest version.");
        }
        if (identity) manifest.identity = *identity;
        if (platform) manifest.host_platform = *platform;
        load_artifact(*fields, "dxc", manifest.dxc, result.diagnostics);
        load_artifact(*fields, "dxc_library", manifest.dxc_library, result.diagnostics);
        load_artifact(*fields, "spirv_val", manifest.spirv_val, result.diagnostics);
        load_artifact(*fields, "spirv_reflect", manifest.spirv_reflect, result.diagnostics);
#if defined(_WIN32)
        load_artifact(*fields, "spirv_reflect_debug", manifest.spirv_reflect_debug, result.diagnostics);
#endif
        load_artifact(*fields, "spirv_reflect_header", manifest.spirv_reflect_header, result.diagnostics);
        load_artifact(*fields, "spirv_header", manifest.spirv_header, result.diagnostics);
        load_metadata(*fields, "d3dcompiler", manifest.d3dcompiler_version, manifest.d3dcompiler_source_url,
            manifest.d3dcompiler_license, result.diagnostics);
        load_metadata(*fields, "dxil_validator", manifest.dxil_validator_version, manifest.dxil_validator_source_url,
            manifest.dxil_validator_license, result.diagnostics);
        if (!result.diagnostics.empty()) return result;
        if (manifest.host_platform != shader_toolchain_host_platform())
        {
            add_error(result.diagnostics, DiagnosticCode::CompilerUnavailable,
                "Shader toolchain host platform does not match this process: expected " +
                    shader_toolchain_host_platform() + ", manifest has " + manifest.host_platform + ".");
            return result;
        }

        DiscoveredShaderToolchain discovered;
        discovered.manifest = std::move(manifest);
        verify_artifact(platform_file, explicit_bundle_root, discovered.manifest.dxc, "DXC", discovered.dxc_path, result.diagnostics);
        verify_artifact(platform_file, explicit_bundle_root, discovered.manifest.dxc_library, "DXC library", discovered.dxc_library_path, result.diagnostics);
        verify_artifact(platform_file, explicit_bundle_root, discovered.manifest.spirv_val, "spirv-val", discovered.spirv_val_path, result.diagnostics);
        verify_artifact(platform_file, explicit_bundle_root, discovered.manifest.spirv_reflect, "SPIRV-Reflect", discovered.spirv_reflect_path, result.diagnostics);
#if defined(_WIN32)
        verify_artifact(platform_file, explicit_bundle_root, discovered.manifest.spirv_reflect_debug, "SPIRV-Reflect Debug", discovered.spirv_reflect_debug_path, result.diagnostics);
#endif
        verify_artifact(platform_file, explicit_bundle_root, discovered.manifest.spirv_reflect_header, "SPIRV-Reflect header", discovered.spirv_reflect_header_path, result.diagnostics);
        verify_artifact(platform_file, explicit_bundle_root, discovered.manifest.spirv_header, "SPIR-V header", discovered.spirv_header_path, result.diagnostics);
        if (!result.diagnostics.empty()) return result;
        result.toolchain = std::move(discovered);
        return result;
    }
}
