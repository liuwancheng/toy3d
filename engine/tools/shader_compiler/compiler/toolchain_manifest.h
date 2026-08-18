#pragma once

#include "format/sha256.h"
#include "frontend/diagnostic.h"
#include "file_system/platform_file.h"

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
        ToolchainArtifact spirv_reflect_debug;
        ToolchainArtifact spirv_reflect_header;
        ToolchainArtifact spirv_header;
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
        PhysicalPath dxc_path;
        PhysicalPath dxc_library_path;
        PhysicalPath spirv_val_path;
        PhysicalPath spirv_reflect_path;
        PhysicalPath spirv_reflect_debug_path;
        PhysicalPath spirv_reflect_header_path;
        PhysicalPath spirv_header_path;
    };

    struct ToolchainDiscoveryResult
    {
        std::optional<DiscoveredShaderToolchain> toolchain;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    std::string shader_toolchain_host_platform();
    FileResult<PhysicalPath> shader_toolchain_root_for_executable(
        const PlatformFile& platform_file,
        const PhysicalPath& executable_path);
    ToolchainDiscoveryResult discover_shader_toolchain(
        const PlatformFile& platform_file,
        const PhysicalPath& explicit_bundle_root);
}
