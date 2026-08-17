#pragma once

#include "common/sha256.h"
#include "frontend/diagnostic.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    constexpr std::uint32_t shader_toolchain_manifest_version = 1;

    struct ToolchainArtifact
    {
        std::string relative_path;
        Sha256Hash content_hash{};
        std::string source_revision;
        std::string build_parameters;
        std::string license;
        std::string source_url;
    };

    struct ShaderToolchainManifest
    {
        std::uint32_t version = shader_toolchain_manifest_version;
        std::string identity;
        std::string host_platform;
        ToolchainArtifact dxc;
        ToolchainArtifact dxc_library;
        ToolchainArtifact spirv_val;
        ToolchainArtifact spirv_reflect;
        ToolchainArtifact spirv_reflect_header;
        std::string d3dcompiler_version;
        std::string d3dcompiler_source_url;
        std::string d3dcompiler_license;
        std::string dxil_validator_version;
        std::string dxil_validator_source_url;
        std::string dxil_validator_license;
    };

    struct DiscoveredShaderToolchain
    {
        ShaderToolchainManifest manifest;
        std::filesystem::path dxc_path;
        std::filesystem::path dxc_library_path;
        std::filesystem::path spirv_val_path;
        std::filesystem::path spirv_reflect_path;
        std::filesystem::path spirv_reflect_header_path;
    };

    struct ToolchainDiscoveryResult
    {
        std::optional<DiscoveredShaderToolchain> toolchain;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    std::string sha256_to_hex(const Sha256Hash& hash);
    std::optional<Sha256Hash> sha256_from_hex(const std::string& text);
    std::string shader_toolchain_host_platform();
    std::filesystem::path shader_toolchain_root_for_executable(
        const std::filesystem::path& executable_path);
    ToolchainDiscoveryResult discover_shader_toolchain(const std::filesystem::path& explicit_bundle_root);
}
