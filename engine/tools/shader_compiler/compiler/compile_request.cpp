#include "compiler/compile_request.h"

#include <sstream>
#include <type_traits>

namespace toy3d::shader
{
    namespace
    {
        template<typename T>
        void append_integer(std::vector<std::uint8_t>& bytes, T value)
        {
            using Unsigned = std::make_unsigned_t<T>;
            const Unsigned converted = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                bytes.push_back(static_cast<std::uint8_t>(converted >> (index * 8u)));
            }
        }

        void append_string(std::vector<std::uint8_t>& bytes, const std::string& value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        bool has_single_stage(ShaderStageFlags stage)
        {
            const std::uint8_t value = static_cast<std::uint8_t>(stage);
            return value != 0 && (value & (value - 1u)) == 0;
        }

        bool target_matches_profile(ShaderTarget target, ShaderCompileProfile profile)
        {
            return (target == ShaderTarget::VulkanSpirV && profile == ShaderCompileProfile::VulkanPortableV1) ||
                (target == ShaderTarget::D3D11Dxbc && profile == ShaderCompileProfile::D3D11FeatureLevel11_0) ||
                (target == ShaderTarget::D3D12Dxil && profile == ShaderCompileProfile::D3D12ShaderModel6);
        }
    }

    bool ShaderCompileRequestResult::succeeded() const
    {
        return request.has_value() && diagnostics.empty();
    }

    ShaderCompileRequestResult build_shader_compile_request(const ShaderCompileRequestInput& input)
    {
        ShaderCompileRequestResult result;
        const SourceLocation location{input.source_virtual_path, 0, 1, 1};
        if (!has_single_stage(input.stage) || input.entry_point.empty() || input.source_virtual_path.empty())
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest, location,
                "Compile request requires one stage, a non-empty entry point, and a virtual source path."});
        }
        if (!target_matches_profile(input.target, input.profile))
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest, location,
                "Shader target and compile profile do not match."});
        }
        if (input.compiler_identity.empty())
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::CompilerUnavailable, location,
                "The locked Shader compiler identity is unavailable."});
        }
        if (!result.diagnostics.empty()) return result;

        std::ostringstream source;
        source << "#line 1 \"/Generated/ToyShaderPrelude.hlsli\"\n" << input.generated_prelude << '\n';
        source << input.generated_bindings << '\n';
        source << "#line 1 \"" << input.source_virtual_path << "\"\n";
        source << input.shader_include_source << '\n' << input.pass_source << '\n';
        if (input.source_provider == nullptr)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error,
                DiagnosticCode::InvalidCompileRequest, {input.source_virtual_path, 0, 1, 1},
                "Shader compilation requires an injected ShaderSourceProvider."});
            return result;
        }
        IncludeResolveResult resolved = resolve_shader_includes(
            source.str(), input.source_virtual_path, *input.source_provider);
        if (!resolved.succeeded())
        {
            result.diagnostics = std::move(resolved.diagnostics);
            return result;
        }

        ShaderCompileRequest request;
        request.target = input.target;
        request.profile = input.profile;
        request.stage = input.stage;
        request.debug_mode = input.debug_mode;
        request.entry_point = input.entry_point;
        request.source_virtual_path = input.source_virtual_path;
        request.compiler_identity = input.compiler_identity;
        request.source = std::move(*resolved.source);
        request.dependencies = std::move(resolved.dependencies);
        request.logical_layout_hash = input.logical_layout_hash;
        request.target_binding_hash = input.target_binding_hash;

        std::vector<std::uint8_t> key;
        append_integer(key, request.version);
        append_integer(key, static_cast<std::uint32_t>(request.target));
        append_integer(key, static_cast<std::uint32_t>(request.profile));
        append_integer(key, static_cast<std::uint32_t>(request.stage));
        append_integer(key, static_cast<std::uint32_t>(request.debug_mode));
        append_string(key, request.entry_point);
        append_string(key, request.source_virtual_path);
        append_string(key, request.compiler_identity);
        key.insert(key.end(), input.logical_layout_hash.begin(), input.logical_layout_hash.end());
        key.insert(key.end(), input.target_binding_hash.begin(), input.target_binding_hash.end());
        append_string(key, request.source);
        for (const ShaderDependency& dependency : request.dependencies)
        {
            append_string(key, dependency.virtual_path);
            key.insert(key.end(), dependency.content_hash.begin(), dependency.content_hash.end());
        }
        request.compile_key = sha256(key);
        result.request = std::move(request);
        return result;
    }
}
