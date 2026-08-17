#include "compiler/compile_request.h"
#include "compiler/dxc_adapter.h"
#include "compiler/toolchain_manifest.h"

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
        return input;
    }

    void test_include_resolution_and_compile_key()
    {
        using namespace toy3d::shader;
        const ShaderCompileRequestResult first = build_shader_compile_request(make_input());
        check(first.succeeded(), "valid Vulkan compile request must build");
        if (!first.request) return;
        check(first.request->dependencies.size() == 2, "transitive virtual includes must be tracked");
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
            "spirv_reflect_header.path=include/spirv_reflect.h\n"
            "spirv_reflect_header.sha256=" + sha256_to_hex(sha256(spirv_val)) + "\n"
            "spirv_reflect_header.source_revision=spirv-reflect-test-commit\n"
            "spirv_reflect_header.build_parameters=public-header\n"
            "spirv_reflect_header.license=Apache-2.0\n"
            "spirv_reflect_header.source_url=https://github.com/KhronosGroup/SPIRV-Reflect\n"
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
        write_bytes(root / "include/spirv_reflect.h", spirv_val);
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
        const VulkanCompileResult compiled = compile_vulkan_shader(*built.request, toolchain, working, runner);
        check(compiled.succeeded(), "DXC adapter must publish binary only after spirv-val succeeds");
        check(invocation_count == 2, "successful Vulkan compile must invoke DXC and spirv-val exactly once");

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
        const VulkanCompileResult validation_failure = compile_vulkan_shader(*built.request, toolchain, working, failing_validator);
        check(!validation_failure.binary && has_diagnostic(validation_failure.diagnostics, DiagnosticCode::ShaderValidationFailed), "spirv-val failure must suppress binary publication");

        const ShaderProcessRunner failing_compiler = [](const std::filesystem::path&, const std::vector<std::string>&) {
            return ProcessResult{true, 1, "compile failed"};
        };
        const VulkanCompileResult compilation_failure = compile_vulkan_shader(*built.request, toolchain, working, failing_compiler);
        check(!compilation_failure.binary && has_diagnostic(compilation_failure.diagnostics, DiagnosticCode::ShaderCompilationFailed), "DXC failure must suppress binary publication");

        toolchain.manifest.identity = "wrong";
        const VulkanCompileResult identity_mismatch = compile_vulkan_shader(*built.request, toolchain, working, runner);
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
            const VulkanCompileResult compiled = compile_vulkan_shader(*vertex_request.request, toolchain, working);
            check(compiled.succeeded(), "explicit DXC must compile minimal vertex HLSL and pass spirv-val");
        }
        if (pixel_request.request)
        {
            const VulkanCompileResult compiled = compile_vulkan_shader(*pixel_request.request, toolchain, working);
            check(compiled.succeeded(), "explicit DXC must compile minimal pixel HLSL and pass spirv-val");
        }
        std::filesystem::remove_all(working);
    }
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
#endif
    if (failure_count != 0)
    {
        std::cerr << failure_count << " Shader compile request test assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Shader compile request tests passed.\n";
    return 0;
}
