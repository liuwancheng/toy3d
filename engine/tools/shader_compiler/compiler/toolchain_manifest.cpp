#include "compiler/toolchain_manifest.h"

#include <cctype>
#include <fstream>
#include <sstream>
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

        bool read_binary_file(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) return false;
            const std::streamoff size = input.tellg();
            if (size < 0) return false;
            bytes.resize(static_cast<std::size_t>(size));
            input.seekg(0, std::ios::beg);
            return bytes.empty() || static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()), size));
        }

        std::optional<std::unordered_map<std::string, std::string>> parse_manifest(
            const std::filesystem::path& path,
            std::vector<Diagnostic>& diagnostics)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                add_error(diagnostics, DiagnosticCode::CompilerUnavailable,
                    "Locked Shader toolchain manifest is missing: " + path.generic_string());
                return std::nullopt;
            }
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
            const std::filesystem::path relative_path(*path);
            if (relative_path.is_absolute() || relative_path.empty())
            {
                add_error(diagnostics, DiagnosticCode::InvalidToolchainManifest,
                    "Toolchain artifact '" + prefix + "' must use a relative bundle path.");
                return false;
            }
            for (const auto& component : relative_path)
            {
                if (component == "..")
                {
                    add_error(diagnostics, DiagnosticCode::InvalidToolchainManifest,
                        "Toolchain artifact '" + prefix + "' escapes the bundle root.");
                    return false;
                }
            }
            artifact.relative_path = relative_path.generic_string();
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
            const std::filesystem::path& bundle_root,
            const ToolchainArtifact& artifact,
            const std::string& name,
            std::filesystem::path& resolved_path,
            std::vector<Diagnostic>& diagnostics)
        {
            resolved_path = bundle_root / std::filesystem::path(artifact.relative_path);
            std::vector<std::uint8_t> bytes;
            if (!read_binary_file(resolved_path, bytes))
            {
                add_error(diagnostics, DiagnosticCode::CompilerUnavailable,
                    "Locked Shader toolchain artifact is missing or unreadable: " + resolved_path.generic_string());
                return false;
            }
            if (sha256(bytes) != artifact.content_hash)
            {
                add_error(diagnostics, DiagnosticCode::ToolchainHashMismatch,
                    "Locked Shader toolchain artifact hash does not match for " + name + ": " + resolved_path.generic_string());
                return false;
            }
            return true;
        }
    }

    bool ToolchainDiscoveryResult::succeeded() const
    {
        return toolchain.has_value() && diagnostics.empty();
    }

    std::string sha256_to_hex(const Sha256Hash& hash)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(hash.size() * 2u);
        for (const std::uint8_t value : hash)
        {
            result.push_back(digits[value >> 4u]);
            result.push_back(digits[value & 0x0fu]);
        }
        return result;
    }

    std::optional<Sha256Hash> sha256_from_hex(const std::string& text)
    {
        if (text.size() != Sha256Hash{}.size() * 2u) return std::nullopt;
        Sha256Hash result{};
        const auto value = [](char character) -> int {
            if (character >= '0' && character <= '9') return character - '0';
            if (character >= 'a' && character <= 'f') return character - 'a' + 10;
            if (character >= 'A' && character <= 'F') return character - 'A' + 10;
            return -1;
        };
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            const int high = value(text[index * 2u]);
            const int low = value(text[index * 2u + 1u]);
            if (high < 0 || low < 0) return std::nullopt;
            result[index] = static_cast<std::uint8_t>((high << 4) | low);
        }
        return result;
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

    std::filesystem::path shader_toolchain_root_for_executable(
        const std::filesystem::path& executable_path)
    {
        return executable_path.parent_path() / "ShaderToolchain" / shader_toolchain_host_platform();
    }

    ToolchainDiscoveryResult discover_shader_toolchain(const std::filesystem::path& explicit_bundle_root)
    {
        ToolchainDiscoveryResult result;
        if (explicit_bundle_root.empty())
        {
            add_error(result.diagnostics, DiagnosticCode::CompilerUnavailable,
                "An explicit Toy3dShaderToolchain bundle root is required.");
            return result;
        }
        const auto fields = parse_manifest(explicit_bundle_root / manifest_file_name, result.diagnostics);
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
        load_artifact(*fields, "spirv_reflect_header", manifest.spirv_reflect_header, result.diagnostics);
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
        verify_artifact(explicit_bundle_root, discovered.manifest.dxc, "DXC", discovered.dxc_path, result.diagnostics);
        verify_artifact(explicit_bundle_root, discovered.manifest.dxc_library, "DXC library", discovered.dxc_library_path, result.diagnostics);
        verify_artifact(explicit_bundle_root, discovered.manifest.spirv_val, "spirv-val", discovered.spirv_val_path, result.diagnostics);
        verify_artifact(explicit_bundle_root, discovered.manifest.spirv_reflect, "SPIRV-Reflect", discovered.spirv_reflect_path, result.diagnostics);
        verify_artifact(explicit_bundle_root, discovered.manifest.spirv_reflect_header, "SPIRV-Reflect header", discovered.spirv_reflect_header_path, result.diagnostics);
        if (!result.diagnostics.empty()) return result;
        result.toolchain = std::move(discovered);
        return result;
    }
}
