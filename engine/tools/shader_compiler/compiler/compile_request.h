#pragma once

#include "compiler/include_resolver.h"
#include "layout/binding_allocator.h"

#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    constexpr std::uint32_t shader_compile_request_version = 1;

    enum class ShaderCompileProfile
    {
        VulkanPortableV1,
        D3D11FeatureLevel11_0,
        D3D12ShaderModel6
    };

    enum class ShaderDebugMode
    {
        Debug,
        Development,
        Shipping
    };

    struct ShaderCompileRequestInput
    {
        ShaderTarget target = ShaderTarget::VulkanSpirV;
        ShaderCompileProfile profile = ShaderCompileProfile::VulkanPortableV1;
        ShaderStageFlags stage = ShaderStageFlags::None;
        ShaderDebugMode debug_mode = ShaderDebugMode::Development;
        std::string entry_point;
        std::string source_virtual_path;
        std::string compiler_identity;
        std::string generated_prelude;
        std::string generated_bindings;
        std::string shader_include_source;
        std::string pass_source;
        const ShaderSourceProvider* source_provider = nullptr;
        Sha256Hash logical_layout_hash{};
        Sha256Hash target_binding_hash{};
    };

    struct ShaderCompileRequest
    {
        std::uint32_t version = shader_compile_request_version;
        ShaderTarget target = ShaderTarget::VulkanSpirV;
        ShaderCompileProfile profile = ShaderCompileProfile::VulkanPortableV1;
        ShaderStageFlags stage = ShaderStageFlags::None;
        ShaderDebugMode debug_mode = ShaderDebugMode::Development;
        std::string entry_point;
        std::string source_virtual_path;
        std::string compiler_identity;
        std::string source;
        std::vector<ShaderDependency> dependencies;
        Sha256Hash logical_layout_hash{};
        Sha256Hash target_binding_hash{};
        Sha256Hash compile_key{};
    };

    struct ShaderCompileRequestResult
    {
        std::optional<ShaderCompileRequest> request;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderCompileRequestResult build_shader_compile_request(const ShaderCompileRequestInput& input);
}
