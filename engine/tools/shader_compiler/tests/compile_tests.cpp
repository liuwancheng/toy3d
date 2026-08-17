#include "shader_map/shader_map_entry.h"
#include "compiler/compile_request.h"
#include "compiler/dxc_adapter.h"
#include "compiler/program_compiler.h"
#include "compiler/shader_compiler.h"
#include "compiler/toolchain_manifest.h"
#include "frontend/shader_parser.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    bool has_diagnostic(const std::vector<toy3d::shader::Diagnostic>& diagnostics, toy3d::shader::DiagnosticCode code)
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const toy3d::shader::Diagnostic& diagnostic) {
            return diagnostic.code == code;
        });
    }

    std::filesystem::path make_test_directory(const std::string& name)
    {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("toy3d_shader_" + name + "_" + std::to_string(unique));
        std::filesystem::create_directories(path);
        return path;
    }

    void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void write_text(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
    }

    std::vector<std::uint8_t> minimal_spirv_header()
    {
        return {
            0x03, 0x02, 0x23, 0x07,
            0x00, 0x03, 0x01, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00};
    }

    toy3d::shader::ShaderCompileRequestInput make_input()
    {
        toy3d::shader::ShaderCompileRequestInput input;
        input.target = toy3d::shader::ShaderTarget::VulkanSpirV;
        input.profile = toy3d::shader::ShaderCompileProfile::VulkanPortableV1;
        input.stage = toy3d::shader::ShaderStageFlags::Vertex;
        input.entry_point = "vs_main";
        input.source_virtual_path = "/Engine/Shaders/Tests/Compile.shader";
        input.compiler_identity = "Toy3dDXC/test";
        input.generated_prelude = "#define TOY3D_TEST 1";
        input.generated_bindings = "float4 test_value;";
        input.shader_include_source = "#include \"/Engine/ShaderIncludes/Nested.hlsli\"";
        input.pass_source = "float4 vs_main() : SV_Position { return included_value; }";
        input.include_files = {
            {"/Engine/ShaderIncludes/Nested.hlsli", "#include \"/Engine/ShaderIncludes/Common.hlsli\"\n"},
            {"/Engine/ShaderIncludes/Common.hlsli", "static const float4 included_value = 1.0;\n"}};
        input.logical_layout_hash[0] = 1u;
        input.target_binding_hash[0] = 1u;
        return input;
    }

    void test_include_resolution_and_compile_key()
    {
        using namespace toy3d::shader;
        const ShaderCompileRequestResult first = build_shader_compile_request(make_input());
        check(first.succeeded(), "valid Vulkan compile request must build");
        if (!first.request) return;
        check(first.request->dependencies.size() == 2, "transitive virtual includes must be tracked");
        check(first.request->logical_layout_hash == make_input().logical_layout_hash &&
            first.request->target_binding_hash == make_input().target_binding_hash,
            "compile request must retain logical and target layout identities for artifact validation");
        check(first.request->dependencies[0].virtual_path == "/Engine/ShaderIncludes/Common.hlsli", "dependencies must be deterministically sorted");
        check(first.request->source.find("#line 1 \"/Engine/ShaderIncludes/Common.hlsli\"") != std::string::npos, "expanded includes must retain virtual #line paths");

        ShaderCompileRequestInput reordered = make_input();
        std::reverse(reordered.include_files.begin(), reordered.include_files.end());
        const ShaderCompileRequestResult second = build_shader_compile_request(reordered);
        check(second.succeeded() && second.request->compile_key == first.request->compile_key, "include registration order must not change the compile key");

        ShaderCompileRequestInput changed = make_input();
        changed.include_files[1].source = "static const float4 included_value = 0.0;\n";
        const ShaderCompileRequestResult changed_result = build_shader_compile_request(changed);
        check(changed_result.succeeded() && changed_result.request->compile_key != first.request->compile_key, "dependency content must enter the compile key");
    }

    void test_include_failures()
    {
        using namespace toy3d::shader;
        ShaderCompileRequestInput relative = make_input();
        relative.shader_include_source = "#include \"Common.hlsli\"";
        const ShaderCompileRequestResult relative_result = build_shader_compile_request(relative);
        check(!relative_result.succeeded() && has_diagnostic(relative_result.diagnostics, DiagnosticCode::InvalidIncludePath), "relative include paths must fail diagnostically");

        ShaderCompileRequestInput cycle = make_input();
        cycle.include_files[1].source = "#include \"/Engine/ShaderIncludes/Nested.hlsli\"\n";
        const ShaderCompileRequestResult cycle_result = build_shader_compile_request(cycle);
        check(!cycle_result.succeeded() && has_diagnostic(cycle_result.diagnostics, DiagnosticCode::IncludeCycle), "include cycles must report the complete failure class");

        ShaderCompileRequestInput generated = make_input();
        generated.shader_include_source = "#include \"/Generated/ToyBindings.hlsli\"";
        const ShaderCompileRequestResult generated_result = build_shader_compile_request(generated);
        check(!generated_result.succeeded() && has_diagnostic(generated_result.diagnostics, DiagnosticCode::InvalidIncludePath), "user HLSL must not include compiler-owned generated paths");
    }

    void test_request_validation()
    {
        using namespace toy3d::shader;
        ShaderCompileRequestInput missing_compiler = make_input();
        missing_compiler.compiler_identity.clear();
        const ShaderCompileRequestResult missing_result = build_shader_compile_request(missing_compiler);
        check(!missing_result.succeeded() && has_diagnostic(missing_result.diagnostics, DiagnosticCode::CompilerUnavailable), "missing locked compiler identity must fail explicitly");

        ShaderCompileRequestInput mismatched = make_input();
        mismatched.profile = ShaderCompileProfile::D3D11FeatureLevel11_0;
        const ShaderCompileRequestResult mismatch_result = build_shader_compile_request(mismatched);
        check(!mismatch_result.succeeded() && has_diagnostic(mismatch_result.diagnostics, DiagnosticCode::InvalidCompileRequest), "target/profile mismatch must fail before adapter invocation");
    }

    std::string make_manifest(
        const std::vector<std::uint8_t>& dxc,
        const std::vector<std::uint8_t>& spirv_val)
    {
        using namespace toy3d::shader;
        return
            "manifest_version=1\n"
            "bundle_identity=Toy3dShaderToolchain/test\n"
            "host_platform=" + shader_toolchain_host_platform() + "\n"
            "dxc.path=bin/dxc\n"
            "dxc.sha256=" + sha256_to_hex(sha256(dxc)) + "\n"
            "dxc.source_revision=dxc-test-commit\n"
            "dxc.build_parameters=-DTOY3D_TEST=ON\n"
            "dxc.license=NCSA\n"
            "dxc.source_url=https://github.com/microsoft/DirectXShaderCompiler\n"
            "dxc_library.path=bin/dxcompiler\n"
            "dxc_library.sha256=" + sha256_to_hex(sha256(dxc)) + "\n"
            "dxc_library.source_revision=dxc-test-commit\n"
            "dxc_library.build_parameters=-DTOY3D_TEST=ON\n"
            "dxc_library.license=NCSA\n"
            "dxc_library.source_url=https://github.com/microsoft/DirectXShaderCompiler\n"
            "spirv_val.path=bin/spirv-val\n"
            "spirv_val.sha256=" + sha256_to_hex(sha256(spirv_val)) + "\n"
            "spirv_val.source_revision=spirv-tools-test-commit\n"
            "spirv_val.build_parameters=-DSPIRV_SKIP_TESTS=ON\n"
            "spirv_val.license=Apache-2.0\n"
            "spirv_val.source_url=https://github.com/KhronosGroup/SPIRV-Tools\n"
            "spirv_reflect.path=lib/spirv-reflect-static\n"
            "spirv_reflect.sha256=" + sha256_to_hex(sha256(spirv_val)) + "\n"
            "spirv_reflect.source_revision=spirv-reflect-test-commit\n"
            "spirv_reflect.build_parameters=static\n"
            "spirv_reflect.license=Apache-2.0\n"
            "spirv_reflect.source_url=https://github.com/KhronosGroup/SPIRV-Reflect\n"
#if defined(_WIN32)
            "spirv_reflect_debug.path=lib/spirv-reflect-static-debug\n"
            "spirv_reflect_debug.sha256=" + sha256_to_hex(sha256(spirv_val)) + "\n"
            "spirv_reflect_debug.source_revision=spirv-reflect-test-commit\n"
            "spirv_reflect_debug.build_parameters=static-debug\n"
            "spirv_reflect_debug.license=Apache-2.0\n"
            "spirv_reflect_debug.source_url=https://github.com/KhronosGroup/SPIRV-Reflect\n"
#endif
            "spirv_reflect_header.path=include/spirv_reflect.h\n"
            "spirv_reflect_header.sha256=" + sha256_to_hex(sha256(spirv_val)) + "\n"
            "spirv_reflect_header.source_revision=spirv-reflect-test-commit\n"
            "spirv_reflect_header.build_parameters=public-header\n"
            "spirv_reflect_header.license=Apache-2.0\n"
            "spirv_reflect_header.source_url=https://github.com/KhronosGroup/SPIRV-Reflect\n"
            "spirv_header.path=include/include/spirv/unified1/spirv.h\n"
            "spirv_header.sha256=" + sha256_to_hex(sha256(spirv_val)) + "\n"
            "spirv_header.source_revision=spirv-reflect-test-commit\n"
            "spirv_header.build_parameters=vendored-public-header\n"
            "spirv_header.license=Apache-2.0\n"
            "spirv_header.source_url=https://github.com/KhronosGroup/SPIRV-Reflect\n"
            "d3dcompiler.version=10.0-test\n"
            "d3dcompiler.source_url=https://developer.microsoft.com/windows/downloads/windows-sdk/\n"
            "d3dcompiler.license=Microsoft Windows SDK\n"
            "dxil_validator.version=test\n"
            "dxil_validator.source_url=https://github.com/microsoft/DirectXShaderCompiler/releases\n"
            "dxil_validator.license=Microsoft binary license\n";
    }

    void test_toolchain_discovery()
    {
        using namespace toy3d::shader;
        const std::filesystem::path root = make_test_directory("toolchain");
        std::filesystem::create_directories(root / "bin");
        const std::vector<std::uint8_t> dxc = {'d', 'x', 'c'};
        const std::vector<std::uint8_t> spirv_val = {'v', 'a', 'l'};
        write_bytes(root / "bin/dxc", dxc);
        write_bytes(root / "bin/dxcompiler", dxc);
        write_bytes(root / "bin/spirv-val", spirv_val);
        std::filesystem::create_directories(root / "lib");
        std::filesystem::create_directories(root / "include");
        write_bytes(root / "lib/spirv-reflect-static", spirv_val);
#if defined(_WIN32)
        write_bytes(root / "lib/spirv-reflect-static-debug", spirv_val);
#endif
        write_bytes(root / "include/spirv_reflect.h", spirv_val);
        std::filesystem::create_directories(root / "include/include/spirv/unified1");
        write_bytes(root / "include/include/spirv/unified1/spirv.h", spirv_val);
        write_text(root / "Toy3dShaderToolchain.manifest", make_manifest(dxc, spirv_val));

        const ToolchainDiscoveryResult discovered = discover_shader_toolchain(root);
        check(discovered.succeeded(), "explicit locked toolchain with matching hashes must be discovered");
        check(discovered.toolchain && discovered.toolchain->manifest.identity == "Toy3dShaderToolchain/test", "manifest compiler identity must be retained");

        std::string wrong_platform_manifest = make_manifest(dxc, spirv_val);
        const std::string expected_platform = "host_platform=" + shader_toolchain_host_platform();
        wrong_platform_manifest.replace(
            wrong_platform_manifest.find(expected_platform), expected_platform.size(), "host_platform=unsupported-test-host");
        write_text(root / "Toy3dShaderToolchain.manifest", wrong_platform_manifest);
        const ToolchainDiscoveryResult wrong_platform = discover_shader_toolchain(root);
        check(!wrong_platform.succeeded() && has_diagnostic(wrong_platform.diagnostics, DiagnosticCode::CompilerUnavailable), "toolchain host platform mismatch must fail before process launch");
        write_text(root / "Toy3dShaderToolchain.manifest", make_manifest(dxc, spirv_val));

        write_text(root / "bin/dxc", "changed");
        const ToolchainDiscoveryResult mismatch = discover_shader_toolchain(root);
        check(!mismatch.succeeded() && has_diagnostic(mismatch.diagnostics, DiagnosticCode::ToolchainHashMismatch), "toolchain hash mismatch must fail diagnostically");

        const ToolchainDiscoveryResult missing = discover_shader_toolchain(root / "missing");
        check(!missing.succeeded() && has_diagnostic(missing.diagnostics, DiagnosticCode::CompilerUnavailable), "missing explicit bundle must not fall back to PATH");
        std::filesystem::remove_all(root);
    }

    void test_toolchain_relative_root()
    {
        using namespace toy3d::shader;
        const std::filesystem::path executable =
            std::filesystem::path("root") / "bin" / "Toy3dShaderCompiler";
        const std::filesystem::path expected =
            std::filesystem::path("root") / "bin" / "ShaderToolchain" / shader_toolchain_host_platform();
        check(shader_toolchain_root_for_executable(executable) == expected,
            "default toolchain root must be relative to the Shader compiler executable");
    }

    void test_dxc_arguments_and_adapter_flow()
    {
        using namespace toy3d::shader;
        ShaderCompileRequestResult built = build_shader_compile_request(make_input());
        check(built.succeeded(), "adapter test compile request must build");
        if (!built.request) return;
        const std::filesystem::path working = make_test_directory("adapter");
        std::vector<Diagnostic> diagnostics;
        const auto invocation = build_vulkan_dxc_invocation(*built.request, working / "input.hlsl", working / "output.spv", diagnostics);
        check(invocation.has_value() && diagnostics.empty(), "Vulkan DXC invocation must build");
        if (invocation)
        {
            const std::vector<std::string>& args = invocation->arguments;
            check(args.size() == 14, "DXC invocation must contain the locked Vulkan argument set");
            check(args[0] == "-spirv" && args[1] == "-fspv-target-env=vulkan1.1" && args[2] == "-fvk-use-dx-layout" && args[3] == "-Zpc", "DXC Vulkan ABI flags must be stable");
            check(std::find(args.begin(), args.end(), "vs_6_0") != args.end(), "vertex request must use the locked vertex profile");
            check(std::find(args.begin(), args.end(), "-fspv-debug=line") != args.end(), "Development mode must retain line debug information");
        }

        DiscoveredShaderToolchain toolchain;
        toolchain.manifest.identity = built.request->compiler_identity;
        toolchain.dxc_path = "locked-dxc";
        toolchain.spirv_val_path = "locked-spirv-val";
        std::size_t invocation_count = 0;
        const ShaderProcessRunner runner = [&](const std::filesystem::path&, const std::vector<std::string>& args) {
            ProcessResult result;
            result.launched = true;
            result.exit_code = 0;
            if (invocation_count++ == 0)
            {
                const auto output = std::find(args.begin(), args.end(), "-Fo");
                if (output != args.end() && output + 1 != args.end()) write_bytes(*(output + 1), minimal_spirv_header());
            }
            return result;
        };
        const ShaderCompilerOutput compiled = compile_vulkan_shader(*built.request, toolchain, working, runner);
        check(compiled.succeeded(), "DXC adapter must publish binary only after spirv-val succeeds");
        check(invocation_count == 2, "successful Vulkan compile must invoke DXC and spirv-val exactly once");

        TargetBindingLayout empty_layout;
        empty_layout.target = ShaderTarget::VulkanSpirV;
        empty_layout.mapping_version = vulkan_binding_mapping_version;
        empty_layout.target_binding_hash[0] = 1u;
        const std::filesystem::path entry_root = working / "shader-code-entries";
        invocation_count = 0;
        const VulkanShaderCodeEntryResult reflection_failure = compile_vulkan_shader_code_entry(
            *built.request, empty_layout, toolchain, working / "reflection", entry_root, runner);
        check(!reflection_failure.succeeded() &&
            has_diagnostic(reflection_failure.diagnostics, DiagnosticCode::ReflectionFailed),
            "invalid final SPIR-V must fail reflection before ShaderCodeEntry publication");
        check(!std::filesystem::exists(entry_root / sha256_to_hex(built.request->compile_key)),
            "reflection failure must not publish a compile-key artifact directory");

        std::size_t failing_invocation = 0;
        const ShaderProcessRunner failing_validator = [&](const std::filesystem::path&, const std::vector<std::string>& args) {
            ProcessResult result;
            result.launched = true;
            result.exit_code = failing_invocation++ == 0 ? 0 : 1;
            result.output = result.exit_code == 0 ? std::string{} : "validation failed";
            if (result.exit_code == 0)
            {
                const auto output = std::find(args.begin(), args.end(), "-Fo");
                if (output != args.end() && output + 1 != args.end()) write_bytes(*(output + 1), minimal_spirv_header());
            }
            return result;
        };
        const ShaderCompilerOutput validation_failure = compile_vulkan_shader(*built.request, toolchain, working, failing_validator);
        check(!validation_failure.binary && has_diagnostic(validation_failure.diagnostics, DiagnosticCode::ShaderValidationFailed), "spirv-val failure must suppress binary publication");

        const ShaderProcessRunner failing_compiler = [](const std::filesystem::path&, const std::vector<std::string>&) {
            return ProcessResult{true, 1, "compile failed"};
        };
        const ShaderCompilerOutput compilation_failure = compile_vulkan_shader(*built.request, toolchain, working, failing_compiler);
        check(!compilation_failure.binary && has_diagnostic(compilation_failure.diagnostics, DiagnosticCode::ShaderCompilationFailed), "DXC failure must suppress binary publication");

        toolchain.manifest.identity = "wrong";
        const ShaderCompilerOutput identity_mismatch = compile_vulkan_shader(*built.request, toolchain, working, runner);
        check(!identity_mismatch.succeeded() && has_diagnostic(identity_mismatch.diagnostics, DiagnosticCode::CompilerUnavailable), "adapter must reject a mismatched compiler identity");
        std::filesystem::remove_all(working);
    }

#if defined(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT) || (defined(TOY3D_SHADER_TEST_DXC) && defined(TOY3D_SHADER_TEST_SPIRV_VAL))
    void test_real_dxc_spirv_integration()
    {
        using namespace toy3d::shader;
        DiscoveredShaderToolchain toolchain;
#if defined(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT)
        const ToolchainDiscoveryResult discovered = discover_shader_toolchain(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT);
        check(discovered.succeeded(), "formal locked toolchain bundle must pass manifest and artifact hash discovery");
        if (!discovered.toolchain) return;
        toolchain = *discovered.toolchain;
#else
        toolchain.manifest.identity = "Toy3dShaderToolchain/explicit-integration-test";
        toolchain.dxc_path = TOY3D_SHADER_TEST_DXC;
        toolchain.spirv_val_path = TOY3D_SHADER_TEST_SPIRV_VAL;
#endif
        const std::filesystem::path working = make_test_directory("real_dxc");
        TargetBindingLayout empty_layout;
        empty_layout.target = ShaderTarget::VulkanSpirV;
        empty_layout.mapping_version = vulkan_binding_mapping_version;
        empty_layout.target_binding_hash[0] = 1u;
        ShaderCompileRequestInput vertex = make_input();
        vertex.compiler_identity = toolchain.manifest.identity;
        vertex.shader_include_source.clear();
        vertex.pass_source = "float4 vs_main(uint vertex_id : SV_VertexID) : SV_Position { return float4(vertex_id == 1 ? 1.0 : -1.0, vertex_id == 2 ? 1.0 : -1.0, 0.0, 1.0); }";
        vertex.include_files.clear();
        ShaderCompileRequestInput pixel = vertex;
        pixel.stage = ShaderStageFlags::Pixel;
        pixel.entry_point = "ps_main";
        pixel.pass_source = "float4 ps_main() : SV_Target0 { return float4(1.0, 0.0, 1.0, 1.0); }";
        const ShaderCompileRequestResult vertex_request = build_shader_compile_request(vertex);
        const ShaderCompileRequestResult pixel_request = build_shader_compile_request(pixel);
        check(vertex_request.succeeded() && pixel_request.succeeded(), "real DXC integration requests must build");
        if (vertex_request.request)
        {
            const VulkanShaderCodeEntryResult compiled = compile_vulkan_shader_code_entry(
                *vertex_request.request, empty_layout, toolchain, working / "compile",
                working / "artifacts");
            check(compiled.succeeded(),
                "explicit DXC vertex output must pass reflection and publish a verified ShaderCodeEntry");
            check(compiled.entry_directory &&
                std::filesystem::exists(*compiled.entry_directory / "manifest.txt") &&
                std::filesystem::exists(*compiled.entry_directory / "shader.spv") &&
                std::filesystem::exists(*compiled.entry_directory / "reflection.txt"),
                "verified vertex ShaderCodeEntry must contain manifest, binary, and reflection records");
        }
        if (pixel_request.request)
        {
            const VulkanShaderCodeEntryResult compiled = compile_vulkan_shader_code_entry(
                *pixel_request.request, empty_layout, toolchain, working / "compile",
                working / "artifacts");
            check(compiled.succeeded(),
                "explicit DXC pixel output must pass reflection and publish a verified ShaderCodeEntry");
        }

        ConstantBufferLayout material_constants;
        material_constants.group = BindingGroup::Material;
        material_constants.size = 112u;
        material_constants.members = {
            {11u, "tint", ShaderValueType::Float32x3, 0u, 12u, 1u, 0u, 0u, {}, {}},
            {12u, "factor", ShaderValueType::Float32, 12u, 4u, 1u, 0u, 0u, {}, {}},
            {13u, "transform", ShaderValueType::Float32x4x4, 16u, 64u, 1u, 0u, 16u, {}, {}},
            {14u, "weights", ShaderValueType::Float32x4, 80u, 32u, 2u, 16u, 0u, {}, {}}};
        ActiveBinding active_constants;
        active_constants.binding_id = 10u;
        active_constants.name = "ToyMaterialConstants";
        active_constants.group = BindingGroup::Material;
        active_constants.category = ShaderParameterCategory::Constant;
        active_constants.stages = ShaderStageFlags::Pixel;
        active_constants.constant_buffer = &material_constants;
        ShaderResourceParameter material_texture;
        material_texture.parameter_id = 20u;
        material_texture.name = "material_texture";
        material_texture.group = BindingGroup::Material;
        material_texture.category = ShaderParameterCategory::SampledTexture;
        material_texture.resource_kind = ResourceKind::Texture2D;
        material_texture.element_type = ResourceElementType::Float4;
        ShaderResourceParameter material_sampler;
        material_sampler.parameter_id = 21u;
        material_sampler.name = "material_sampler";
        material_sampler.group = BindingGroup::Material;
        material_sampler.category = ShaderParameterCategory::Sampler;
        material_sampler.resource_kind = ResourceKind::Sampler;
        ActiveBinding active_texture;
        active_texture.binding_id = material_texture.parameter_id;
        active_texture.name = material_texture.name;
        active_texture.group = material_texture.group;
        active_texture.category = material_texture.category;
        active_texture.stages = ShaderStageFlags::Pixel;
        active_texture.resource = &material_texture;
        ActiveBinding active_sampler;
        active_sampler.binding_id = material_sampler.parameter_id;
        active_sampler.name = material_sampler.name;
        active_sampler.group = material_sampler.group;
        active_sampler.category = material_sampler.category;
        active_sampler.stages = ShaderStageFlags::Pixel;
        active_sampler.resource = &material_sampler;
        TargetBindingLayout resource_layout;
        resource_layout.target = ShaderTarget::VulkanSpirV;
        resource_layout.mapping_version = vulkan_binding_mapping_version;
        resource_layout.target_binding_hash[0] = 2u;
        resource_layout.bindings = {
            {10u, "ToyMaterialConstants", BindingGroup::Material, ShaderParameterCategory::Constant,
                ShaderStageFlags::Pixel, NativeRegisterClass::ConstantBuffer, 0u, 2u, 0u, &active_constants},
            {20u, "material_texture", BindingGroup::Material, ShaderParameterCategory::SampledTexture,
                ShaderStageFlags::Pixel, NativeRegisterClass::ShaderResource, 0u, 2u, 1u, &active_texture},
            {21u, "material_sampler", BindingGroup::Material, ShaderParameterCategory::Sampler,
                ShaderStageFlags::Pixel, NativeRegisterClass::Sampler, 0u, 2u, 2u, &active_sampler}};
        ShaderCompileRequestInput resource_pixel = pixel;
        resource_pixel.generated_bindings =
            "[[vk::binding(0, 2)]]\n"
            "cbuffer ToyMaterialConstants : register(b0)\n"
            "{\n"
            "    float3 tint : packoffset(c0);\n"
            "    float factor : packoffset(c0.w);\n"
            "    column_major float4x4 transform : packoffset(c1);\n"
            "    float4 weights[2] : packoffset(c5);\n"
            "};\n"
            "[[vk::binding(1, 2)]] Texture2D<float4> material_texture : register(t0);\n"
            "[[vk::binding(2, 2)]] SamplerState material_sampler : register(s0);\n";
        resource_pixel.pass_source =
            "float4 ps_main(float2 uv : TEXCOORD0) : SV_Target0 "
            "{ return material_texture.Sample(material_sampler, uv) * "
            "(mul(transform, float4(tint * factor, 1.0)) + weights[1]); }";
        resource_pixel.target_binding_hash = resource_layout.target_binding_hash;
        const ShaderCompileRequestResult resource_request = build_shader_compile_request(resource_pixel);
        check(resource_request.succeeded(), "resource reflection integration request must build");
        if (resource_request.request)
        {
            const VulkanShaderCodeEntryResult compiled = compile_vulkan_shader_code_entry(
                *resource_request.request, resource_layout, toolchain, working / "compile",
                working / "resource-artifacts");
            check(compiled.succeeded(),
                "SPIRV-Reflect must validate constant offsets, resource types, and Vulkan set/binding mapping");

            TargetBindingLayout mismatched_layout = resource_layout;
            mismatched_layout.bindings[1].descriptor_binding = 7u;
            const VulkanShaderCodeEntryResult mismatch = compile_vulkan_shader_code_entry(
                *resource_request.request, mismatched_layout, toolchain, working / "compile-mismatch",
                working / "mismatch-artifacts");
            check(!mismatch.succeeded() &&
                has_diagnostic(mismatch.diagnostics, DiagnosticCode::ReflectionUnexpectedResource),
                "native set/binding mismatch must fail parity validation");
            check(!std::filesystem::exists(working / "mismatch-artifacts" /
                sha256_to_hex(resource_request.request->compile_key)),
                "parity mismatch must not publish a ShaderCodeEntry");
        }
        std::filesystem::remove_all(working);
    }

#if defined(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT)
    void test_real_program_compiler()
    {
        using namespace toy3d::shader;
        const ToolchainDiscoveryResult discovered =
            discover_shader_toolchain(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT);
        check(discovered.succeeded(), "Program compiler requires the locked toolchain bundle");
        if (!discovered.toolchain) return;

        const std::string source = R"(
Shader "Tests/ProgramCompile"
{
    Version 1
    Properties
    {
        tint ("Tint", Color) = (1.0, 1.0, 1.0, 1.0)
        source_texture ("Source", Texture2D) = "white"
        source_sampler ("Sampler", Sampler) = LinearClamp
    }
    Pass "Forward"
    {
        HLSLPROGRAM
        #pragma vertex vs_main
        #pragma pixel ps_main
        struct Varyings
        {
            float4 position : SV_Position;
            float2 uv : TEXCOORD0;
        };
        Varyings vs_main(uint vertex_id : SV_VertexID)
        {
            Varyings output;
            output.position = float4(vertex_id == 1 ? 1.0 : -1.0,
                vertex_id == 2 ? 1.0 : -1.0, 0.0, 1.0);
            output.uv = float2(0.5, 0.5);
            return output;
        }
        float4 ps_main(Varyings input) : SV_Target0
        {
            return source_texture.Sample(source_sampler, input.uv) * tint;
        }
        ENDHLSL
    }
})";
        const ParseResult parsed = parse_shader(
            source, "/Engine/Shaders/Tests/ProgramCompile.shader");
        check(parsed.succeeded(), "Program compiler test Shader must parse");
        if (!parsed.asset) return;

        ShaderProgramCompileInput input;
        input.pass_name = "Forward";
        input.source_virtual_path = "/Engine/Shaders/Tests/ProgramCompile.shader";
        const std::filesystem::path working = make_test_directory("program_compile");
        const ShaderMapEntryCompileResult compiled = compile_vulkan_shader_map_entry(
            *parsed.asset, input, *discovered.toolchain, working);
        for (const Diagnostic& diagnostic : compiled.diagnostics)
        {
            std::cerr << format_diagnostic(diagnostic) << '\n';
        }
        check(compiled.succeeded(),
            "Program compiler must discover active bindings and complete the strict final compile");
        if (compiled.entry)
        {
            check(compiled.entry->stages.size() == 2u,
                "Graphics ShaderMapEntry must contain vertex and pixel ShaderCodeEntry records");
            check(compiled.entry->bindings.size() == 3u,
                "Only the active Material cbuffer, texture, and sampler must remain");
            check(std::all_of(compiled.entry->bindings.begin(), compiled.entry->bindings.end(),
                [](const ShaderMapBinding& binding) {
                    return binding.stages == ShaderStageFlags::Pixel;
                }), "Discovery must compute final pixel-only stage visibility");
            check(compiled.entry->bindings[0].descriptor_set == 2u &&
                compiled.entry->bindings[0].descriptor_binding == 0u &&
                compiled.entry->bindings[1].descriptor_binding == 1u &&
                compiled.entry->bindings[2].descriptor_binding == 2u,
                "Final Vulkan Material bindings must be compact and deterministic");
            const ShaderMapEntryWriteResult entry_write = write_verified_shader_map_entry(
                working / "shader-map", *compiled.entry);
            check(entry_write.succeeded(),
                "Strictly validated Program compilation must publish one atomic ShaderMapEntry");
            check(entry_write.entry_directory &&
                std::filesystem::exists(*entry_write.entry_directory / "manifest.txt") &&
                std::filesystem::exists(*entry_write.entry_directory / "mapping.txt") &&
                std::filesystem::exists(*entry_write.entry_directory / "vertex.spv") &&
                std::filesystem::exists(*entry_write.entry_directory / "pixel.spv"),
                "ShaderMapEntry must contain manifest, mapping, and all stage binaries");
            const ShaderMapEntryWriteResult duplicate = write_verified_shader_map_entry(
                working / "shader-map", *compiled.entry);
            check(!duplicate.succeeded() &&
                has_diagnostic(duplicate.diagnostics, DiagnosticCode::ShaderCodeWriteFailed),
                "An existing ShaderMapEntry key must fail deterministically before reader cache hits are implemented");
        }
        if (compiled.succeeded())
        {
            std::filesystem::remove_all(working);
        }
        else
        {
            std::cerr << "Program compiler working directory: " << working << '\n';
        }
    }
#endif
#endif
}

int main()
{
    test_include_resolution_and_compile_key();
    test_include_failures();
    test_request_validation();
    test_toolchain_discovery();
    test_toolchain_relative_root();
    test_dxc_arguments_and_adapter_flow();
#if defined(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT) || (defined(TOY3D_SHADER_TEST_DXC) && defined(TOY3D_SHADER_TEST_SPIRV_VAL))
    test_real_dxc_spirv_integration();
#if defined(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT)
    test_real_program_compiler();
#endif
#endif
    if (failure_count != 0)
    {
        std::cerr << failure_count << " Shader compile request test assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Shader compile request tests passed.\n";
    return 0;
}
