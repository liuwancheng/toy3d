#include "shader_map/shader_map_entry.h"
#include "compiler/compile_request.h"
#include "compiler/dxc_adapter.h"
#include "compiler/program_compiler.h"
#include "compiler/shader_compiler.h"
#include "compiler/toolchain_manifest.h"
#include "compiler/variant_permutation.h"
#include "frontend/shader_parser.h"
#include "file_system/native_platform_file.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    // filesystem manages isolated compiler fixtures and artifact assertions in
    // tests; production compiler paths still flow through PlatformFile.
    int failure_count = 0;
    toy3d::NativePlatformFile platform_file;

    void check(bool condition, const std::string& message);
    bool has_diagnostic(const std::vector<toy3d::shader::Diagnostic>& diagnostics, toy3d::shader::DiagnosticCode code);

    void test_variant_permutation_contract()
    {
        using namespace toy3d::shader;
        ShaderAsset asset;
        asset.name = "Toy3d/Tests/Variants";
        asset.variants = {
            {VariantType::Boolean, "USE_NORMAL_MAP", {}, "false", {}},
            {VariantType::Enumeration, "LIGHTING_MODEL", {"Unlit", "DefaultLit", "ClearCoat"}, "DefaultLit", {}}};
        check(make_shader_variant_id("USE_NORMAL_MAP") == 0xf17f0b805c4eae94ull,
              "ShaderVariantId v1 must retain its golden value");
        check(make_shader_enum_value_id(make_shader_variant_id("LIGHTING_MODEL"), "DefaultLit") ==
                  0x75c1de6eb41f9e57ull,
              "ShaderEnumValueId v1 must retain its golden value");

        const ShaderPermutationResult defaults = resolve_shader_permutation(asset, {});
        check(defaults.succeeded(), "Variant schema defaults must resolve to one typed permutation");
        check(defaults.permutation && defaults.permutation->records.size() == 2u,
              "Every declared Variant must produce one canonical permutation record");
        check(defaults.permutation && toy3d::sha256_to_hex(defaults.permutation->key) ==
                                          "6a67420b795afb9f008e5710bbc472fafe5bdea607dc887dccbb79fb838905a7",
              "Permutation ABI v1 must retain its golden default key");
        check(defaults.permutation && defaults.permutation->generated_prelude.find(
                                          "#define TOY3D_VARIANT_USE_NORMAL_MAP 0") != std::string::npos,
              "Boolean Variant default must generate a 0/1 macro");
        check(defaults.permutation && defaults.permutation->generated_prelude.find(
                                          "#define TOY3D_VARIANT_LIGHTING_MODEL_DefaultLit") != std::string::npos,
              "Enum Variant options must generate compiler-owned symbolic macros");

        const ShaderPermutationResult selected =
            resolve_shader_permutation(asset, {{"LIGHTING_MODEL", "ClearCoat"}, {"USE_NORMAL_MAP", "true"}});
        check(selected.succeeded() && defaults.permutation && selected.permutation &&
                  selected.permutation->key != defaults.permutation->key,
              "Changing a typed Variant value must change the permutation key");

        ShaderAsset reordered = asset;
        std::reverse(reordered.variants.begin(), reordered.variants.end());
        std::reverse(reordered.variants.front().options.begin(), reordered.variants.front().options.end());
        const ShaderPermutationResult stable =
            resolve_shader_permutation(reordered, {{"USE_NORMAL_MAP", "true"}, {"LIGHTING_MODEL", "ClearCoat"}});
        check(stable.succeeded() && selected.permutation && stable.permutation &&
                  stable.permutation->key == selected.permutation->key &&
                  stable.permutation->generated_prelude == selected.permutation->generated_prelude,
              "Variant declaration and enum option order must not change canonical identity or macros");

        const ShaderPermutationResult duplicate =
            resolve_shader_permutation(asset, {{"USE_NORMAL_MAP", "true"}, {"USE_NORMAL_MAP", "false"}});
        check(!duplicate.succeeded() && has_diagnostic(duplicate.diagnostics, DiagnosticCode::InvalidVariantSelection),
              "Duplicate Variant selections must fail diagnostically");
        const ShaderPermutationResult unknown = resolve_shader_permutation(asset, {{"UNKNOWN", "true"}});
        check(!unknown.succeeded() && has_diagnostic(unknown.diagnostics, DiagnosticCode::InvalidVariantSelection),
              "Unknown Variant selections must fail diagnostically");
        const ShaderPermutationResult invalid = resolve_shader_permutation(asset, {{"LIGHTING_MODEL", "Invalid"}});
        check(!invalid.succeeded() && has_diagnostic(invalid.diagnostics, DiagnosticCode::InvalidVariantSelection),
              "Invalid enum Variant values must fail diagnostically");

        ShaderAsset empty;
        const ShaderPermutationResult empty_domain = resolve_shader_permutation(empty, {});
        check(empty_domain.succeeded() && empty_domain.permutation &&
                  std::any_of(empty_domain.permutation->key.begin(), empty_domain.permutation->key.end(),
                              [](std::uint8_t byte) { return byte != 0u; }),
              "An empty Variant domain must still have a versioned non-zero permutation key");
    }

    toy3d::PhysicalPath physical_path(const std::filesystem::path& path)
    {
        return toy3d::PhysicalPath(path.u8string());
    }

    std::vector<toy3d::shader::VirtualIncludeFile> default_include_files()
    {
        return {{"/Engine/ShaderIncludes/Nested.hlsli", "#include \"/Engine/ShaderIncludes/Common.hlsli\"\n"},
                {"/Engine/ShaderIncludes/Common.hlsli", "static const float4 included_value = 1.0;\n"}};
    }

    class FaultInjectingPlatformFile final : public toy3d::PlatformFile
    {
      public:
        enum class Failure
        {
            None,
            WriteText,
            Rename
        };

        explicit FaultInjectingPlatformFile(Failure failure) : failure_(failure) {}

        toy3d::PlatformFileCapabilities capabilities() const override { return native_.capabilities(); }

        toy3d::FileResult<std::unique_ptr<toy3d::FileHandle>> open(const toy3d::PhysicalPath& path,
                                                                   toy3d::FileOpenMode mode) const override
        {
            return native_.open(path, mode);
        }

        toy3d::FileResult<toy3d::FileStat> stat(const toy3d::PhysicalPath& path) const override
        {
            return native_.stat(path);
        }

        toy3d::FileResult<bool> exists(const toy3d::PhysicalPath& path) const override { return native_.exists(path); }

        toy3d::FileResult<std::vector<std::uint8_t>> read_binary(const toy3d::PhysicalPath& path) const override
        {
            return native_.read_binary(path);
        }

        toy3d::FileResult<std::string> read_text_utf8(const toy3d::PhysicalPath& path) const override
        {
            return native_.read_text_utf8(path);
        }

        toy3d::FileStatus write_text_utf8(const toy3d::PhysicalPath& path, const std::string& text,
                                          toy3d::FileWriteMode mode) override
        {
            if (failure_ == Failure::WriteText)
            {
                failure_ = Failure::None;
                return injected_error("write_text_utf8", path);
            }
            return native_.write_text_utf8(path, text, mode);
        }

        toy3d::FileStatus write_binary(const toy3d::PhysicalPath& path, const std::vector<std::uint8_t>& bytes,
                                       toy3d::FileWriteMode mode) override
        {
            return native_.write_binary(path, bytes, mode);
        }

        toy3d::FileStatus create_directory(const toy3d::PhysicalPath& path) override
        {
            return native_.create_directory(path);
        }

        toy3d::FileStatus create_directories(const toy3d::PhysicalPath& path) override
        {
            return native_.create_directories(path);
        }

        toy3d::FileStatus rename_no_replace(const toy3d::PhysicalPath& source,
                                            const toy3d::PhysicalPath& destination) override
        {
            if (failure_ == Failure::Rename)
            {
                failure_ = Failure::None;
                return injected_error("rename_no_replace", destination);
            }
            return native_.rename_no_replace(source, destination);
        }

        toy3d::FileStatus replace(const toy3d::PhysicalPath& source, const toy3d::PhysicalPath& destination) override
        {
            return native_.replace(source, destination);
        }

        toy3d::FileStatus remove_file(const toy3d::PhysicalPath& path) override { return native_.remove_file(path); }

        toy3d::FileStatus remove_empty_directory(const toy3d::PhysicalPath& path) override
        {
            return native_.remove_empty_directory(path);
        }

        toy3d::FileResult<std::uintmax_t> remove_directory_tree(const toy3d::PhysicalPath& path) override
        {
            return native_.remove_directory_tree(path);
        }

        toy3d::FileResult<std::vector<toy3d::DirectoryEntry>> enumerate_directory(
            const toy3d::PhysicalPath& path) const override
        {
            return native_.enumerate_directory(path);
        }

        toy3d::FileResult<toy3d::PhysicalPath> absolute(const toy3d::PhysicalPath& path) const override
        {
            return native_.absolute(path);
        }

        toy3d::FileResult<toy3d::PhysicalPath> lexically_normal(const toy3d::PhysicalPath& path) const override
        {
            return native_.lexically_normal(path);
        }

        toy3d::FileResult<toy3d::PhysicalPath> canonical(const toy3d::PhysicalPath& path) const override
        {
            return native_.canonical(path);
        }

        toy3d::FileResult<toy3d::PhysicalPath> parent_path(const toy3d::PhysicalPath& path) const override
        {
            return native_.parent_path(path);
        }

        toy3d::FileResult<toy3d::PhysicalPath> join_relative(const toy3d::PhysicalPath& base,
                                                             const std::string& relative) const override
        {
            return native_.join_relative(base, relative);
        }

      private:
        static toy3d::FileStatus injected_error(const std::string& operation, const toy3d::PhysicalPath& path)
        {
            toy3d::FileStatus status;
            status.code = toy3d::FileErrorCode::IoError;
            status.operation = operation;
            status.path = path;
            status.message = "injected test failure";
            return status;
        }

        toy3d::NativePlatformFile native_;
        Failure failure_ = Failure::None;
    };

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
        return std::any_of(diagnostics.begin(), diagnostics.end(),
                           [&](const toy3d::shader::Diagnostic& diagnostic) { return diagnostic.code == code; });
    }

    std::filesystem::path make_test_directory(const std::string& name)
    {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("toy3d_shader_" + name + "_" + std::to_string(unique));
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
        return {0x03, 0x02, 0x23, 0x07, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }

    toy3d::shader::ShaderCompileRequestInput make_input(const toy3d::shader::ShaderSourceProvider& source_provider)
    {
        toy3d::shader::ShaderCompileRequestInput input;
        input.target = toy3d::shader::ShaderTarget::VulkanSpirV;
        input.profile = toy3d::shader::ShaderCompileProfile::VulkanES31;
        input.stage = toy3d::shader::ShaderStageFlags::Vertex;
        input.entry_point = "vs_main";
        input.source_virtual_path = "/Engine/Shaders/Tests/Compile.shader";
        input.compiler_identity = "Toy3dDXC/test";
        input.generated_prelude = "#define TOY3D_TEST 1";
        input.generated_bindings = "float4 test_value;";
        input.shader_include_source = "#include \"/Engine/ShaderIncludes/Nested.hlsli\"";
        input.pass_source = "float4 vs_main() : SV_Position { return included_value; }";
        input.source_provider = &source_provider;
        input.logical_layout_hash[0] = 1u;
        input.target_binding_hash[0] = 1u;
        return input;
    }

    void test_include_resolution_and_compile_key()
    {
        using namespace toy3d::shader;
        const RegisteredShaderSourceProvider first_provider(default_include_files());
        const ShaderCompileRequestInput first_input = make_input(first_provider);
        const ShaderCompileRequestResult first = build_shader_compile_request(first_input);
        check(first.succeeded(), "valid Vulkan compile request must build");
        if (!first.request)
            return;
        check(first.request->dependencies.size() == 2, "transitive virtual includes must be tracked");
        check(first.request->logical_layout_hash == first_input.logical_layout_hash &&
                  first.request->target_binding_hash == first_input.target_binding_hash,
              "compile request must retain logical and target layout identities for artifact validation");
        check(first.request->dependencies[0].virtual_path == "/Engine/ShaderIncludes/Common.hlsli",
              "dependencies must be deterministically sorted");
        check(first.request->source.find("#line 1 \"/Engine/ShaderIncludes/Common.hlsli\"") != std::string::npos,
              "expanded includes must retain virtual #line paths");

        std::vector<VirtualIncludeFile> reordered_files = default_include_files();
        std::reverse(reordered_files.begin(), reordered_files.end());
        const RegisteredShaderSourceProvider reordered_provider(std::move(reordered_files));
        ShaderCompileRequestInput reordered = make_input(reordered_provider);
        const ShaderCompileRequestResult second = build_shader_compile_request(reordered);
        check(second.succeeded() && second.request->compile_key == first.request->compile_key,
              "include registration order must not change the compile key");

        std::vector<VirtualIncludeFile> changed_files = default_include_files();
        changed_files[1].source = "static const float4 included_value = 0.0;\n";
        const RegisteredShaderSourceProvider changed_provider(std::move(changed_files));
        ShaderCompileRequestInput changed = make_input(changed_provider);
        const ShaderCompileRequestResult changed_result = build_shader_compile_request(changed);
        check(changed_result.succeeded() && changed_result.request->compile_key != first.request->compile_key,
              "dependency content must enter the compile key");
        const RegisteredShaderSourceProvider project_provider({
            {"/Project/ShaderIncludes/Common.hlsli", "static const float4 included_value = 1.0;\n"}});
        auto project = make_input(project_provider);
        project.shader_include_source.clear();
        project.pass_source_line = 70u;
        project.pass_source = "#include \"/Project/ShaderIncludes/Common.hlsli\"\nfloat4 vs_main() : SV_Position { return included_value; }";
        const auto project_result = build_shader_compile_request(project);
        check(project_result.succeeded() && project_result.request->dependencies.size() == 1u &&
            project_result.request->source.find("#line 71 \"/Engine/Shaders/Tests/Compile.shader\"") != std::string::npos,
            "project include expansion must restore the author's .shader line after include");
    }

    void test_include_failures()
    {
        using namespace toy3d::shader;
        const RegisteredShaderSourceProvider provider(default_include_files());
        ShaderCompileRequestInput relative = make_input(provider);
        relative.shader_include_source = "#include \"Common.hlsli\"";
        const ShaderCompileRequestResult relative_result = build_shader_compile_request(relative);
        check(!relative_result.succeeded() &&
                  has_diagnostic(relative_result.diagnostics, DiagnosticCode::InvalidIncludePath),
              "relative include paths must fail diagnostically");

        std::vector<VirtualIncludeFile> cycle_files = default_include_files();
        cycle_files[1].source = "#include \"/Engine/ShaderIncludes/Nested.hlsli\"\n";
        const RegisteredShaderSourceProvider cycle_provider(std::move(cycle_files));
        ShaderCompileRequestInput cycle = make_input(cycle_provider);
        const ShaderCompileRequestResult cycle_result = build_shader_compile_request(cycle);
        check(!cycle_result.succeeded() && has_diagnostic(cycle_result.diagnostics, DiagnosticCode::IncludeCycle),
              "include cycles must report the complete failure class");

        ShaderCompileRequestInput generated = make_input(provider);
        generated.shader_include_source = "#include \"/Generated/ToyBindings.hlsli\"";
        const ShaderCompileRequestResult generated_result = build_shader_compile_request(generated);
        check(!generated_result.succeeded() &&
                  has_diagnostic(generated_result.diagnostics, DiagnosticCode::InvalidIncludePath),
              "user HLSL must not include compiler-owned generated paths");
    }

    void test_request_validation()
    {
        using namespace toy3d::shader;
        const RegisteredShaderSourceProvider provider(default_include_files());
        ShaderCompileRequestInput missing_compiler = make_input(provider);
        missing_compiler.compiler_identity.clear();
        const ShaderCompileRequestResult missing_result = build_shader_compile_request(missing_compiler);
        check(!missing_result.succeeded() &&
                  has_diagnostic(missing_result.diagnostics, DiagnosticCode::CompilerUnavailable),
              "missing locked compiler identity must fail explicitly");

        ShaderCompileRequestInput mismatched = make_input(provider);
        mismatched.profile = ShaderCompileProfile::D3D11FeatureLevel11_0;
        const ShaderCompileRequestResult mismatch_result = build_shader_compile_request(mismatched);
        check(!mismatch_result.succeeded() &&
                  has_diagnostic(mismatch_result.diagnostics, DiagnosticCode::InvalidCompileRequest),
              "target/profile mismatch must fail before adapter invocation");
    }

    std::string make_manifest(const std::vector<std::uint8_t>& dxc, const std::vector<std::uint8_t>& spirv_val)
    {
        using namespace toy3d::shader;
        return "manifest_version=1\n"
               "bundle_identity=Toy3dShaderToolchain/test\n"
               "host_platform=" +
               shader_toolchain_host_platform() +
               "\n"
               "dxc.path=bin/dxc\n"
               "dxc.sha256=" +
               toy3d::sha256_to_hex(toy3d::sha256(dxc)) +
               "\n"
               "dxc.source_revision=dxc-test-commit\n"
               "dxc.build_parameters=-DTOY3D_TEST=ON\n"
               "dxc.license=NCSA\n"
               "dxc.source_url=https://github.com/microsoft/DirectXShaderCompiler\n"
               "dxc_library.path=bin/dxcompiler\n"
               "dxc_library.sha256=" +
               toy3d::sha256_to_hex(toy3d::sha256(dxc)) +
               "\n"
               "dxc_library.source_revision=dxc-test-commit\n"
               "dxc_library.build_parameters=-DTOY3D_TEST=ON\n"
               "dxc_library.license=NCSA\n"
               "dxc_library.source_url=https://github.com/microsoft/DirectXShaderCompiler\n"
               "spirv_val.path=bin/spirv-val\n"
               "spirv_val.sha256=" +
               toy3d::sha256_to_hex(toy3d::sha256(spirv_val)) +
               "\n"
               "spirv_val.source_revision=spirv-tools-test-commit\n"
               "spirv_val.build_parameters=-DSPIRV_SKIP_TESTS=ON\n"
               "spirv_val.license=Apache-2.0\n"
               "spirv_val.source_url=https://github.com/KhronosGroup/SPIRV-Tools\n"
               "spirv_reflect.path=lib/spirv-reflect-static\n"
               "spirv_reflect.sha256=" +
               toy3d::sha256_to_hex(toy3d::sha256(spirv_val)) +
               "\n"
               "spirv_reflect.source_revision=spirv-reflect-test-commit\n"
               "spirv_reflect.build_parameters=static\n"
               "spirv_reflect.license=Apache-2.0\n"
               "spirv_reflect.source_url=https://github.com/KhronosGroup/SPIRV-Reflect\n"
#if defined(_WIN32)
               "spirv_reflect_debug.path=lib/spirv-reflect-static-debug\n"
               "spirv_reflect_debug.sha256=" +
               toy3d::sha256_to_hex(toy3d::sha256(spirv_val)) +
               "\n"
               "spirv_reflect_debug.source_revision=spirv-reflect-test-commit\n"
               "spirv_reflect_debug.build_parameters=static-debug\n"
               "spirv_reflect_debug.license=Apache-2.0\n"
               "spirv_reflect_debug.source_url=https://github.com/KhronosGroup/SPIRV-Reflect\n"
#endif
               "spirv_reflect_header.path=include/spirv_reflect.h\n"
               "spirv_reflect_header.sha256=" +
               toy3d::sha256_to_hex(toy3d::sha256(spirv_val)) +
               "\n"
               "spirv_reflect_header.source_revision=spirv-reflect-test-commit\n"
               "spirv_reflect_header.build_parameters=public-header\n"
               "spirv_reflect_header.license=Apache-2.0\n"
               "spirv_reflect_header.source_url=https://github.com/KhronosGroup/SPIRV-Reflect\n"
               "spirv_header.path=include/include/spirv/unified1/spirv.h\n"
               "spirv_header.sha256=" +
               toy3d::sha256_to_hex(toy3d::sha256(spirv_val)) +
               "\n"
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

        const ToolchainDiscoveryResult discovered = discover_shader_toolchain(platform_file, physical_path(root));
        check(discovered.succeeded(), "explicit locked toolchain with matching hashes must be discovered");
        check(discovered.toolchain && discovered.toolchain->manifest.identity == "Toy3dShaderToolchain/test",
              "manifest compiler identity must be retained");

        std::string wrong_platform_manifest = make_manifest(dxc, spirv_val);
        const std::string expected_platform = "host_platform=" + shader_toolchain_host_platform();
        wrong_platform_manifest.replace(wrong_platform_manifest.find(expected_platform), expected_platform.size(),
                                        "host_platform=unsupported-test-host");
        write_text(root / "Toy3dShaderToolchain.manifest", wrong_platform_manifest);
        const ToolchainDiscoveryResult wrong_platform = discover_shader_toolchain(platform_file, physical_path(root));
        check(!wrong_platform.succeeded() &&
                  has_diagnostic(wrong_platform.diagnostics, DiagnosticCode::CompilerUnavailable),
              "toolchain host platform mismatch must fail before process launch");
        write_text(root / "Toy3dShaderToolchain.manifest", make_manifest(dxc, spirv_val));

        write_text(root / "bin/dxc", "changed");
        const ToolchainDiscoveryResult mismatch = discover_shader_toolchain(platform_file, physical_path(root));
        check(!mismatch.succeeded() && has_diagnostic(mismatch.diagnostics, DiagnosticCode::ToolchainHashMismatch),
              "toolchain hash mismatch must fail diagnostically");

        const ToolchainDiscoveryResult missing =
            discover_shader_toolchain(platform_file, physical_path(root / "missing"));
        check(!missing.succeeded() && has_diagnostic(missing.diagnostics, DiagnosticCode::CompilerUnavailable),
              "missing explicit bundle must not fall back to PATH");
        std::filesystem::remove_all(root);
    }

    void test_toolchain_relative_root()
    {
        using namespace toy3d::shader;
        const std::filesystem::path executable = std::filesystem::path("root") / "bin" / "Toy3dShaderCompiler";
        const std::filesystem::path expected =
            std::filesystem::path("root") / "bin" / "ShaderToolchain" / shader_toolchain_host_platform();
        const toy3d::FileResult<toy3d::PhysicalPath> actual =
            shader_toolchain_root_for_executable(platform_file, physical_path(executable));
        check(actual.succeeded() && actual.value() == physical_path(expected),
              "default toolchain root must be relative to the Shader compiler executable");
    }

    void test_dxc_arguments_and_adapter_flow()
    {
        using namespace toy3d::shader;
        const RegisteredShaderSourceProvider provider(default_include_files());
        ShaderCompileRequestResult built = build_shader_compile_request(make_input(provider));
        check(built.succeeded(), "adapter test compile request must build");
        if (!built.request)
            return;
        const std::filesystem::path working = make_test_directory("adapter");
        std::vector<Diagnostic> diagnostics;
        const auto invocation = build_vulkan_dxc_invocation(*built.request, physical_path(working / "input.hlsl"),
                                                            physical_path(working / "output.spv"), diagnostics);
        check(invocation.has_value() && diagnostics.empty(), "Vulkan DXC invocation must build");
        if (invocation)
        {
            const std::vector<std::string>& args = invocation->arguments;
            check(args.size() == 14, "DXC invocation must contain the locked Vulkan argument set");
            check(args[0] == "-spirv" && args[1] == "-fspv-target-env=vulkan1.1" && args[2] == "-fvk-use-dx-layout" &&
                      args[3] == "-Zpc",
                  "DXC Vulkan ABI flags must be stable");
            check(std::find(args.begin(), args.end(), "vs_6_0") != args.end(),
                  "vertex request must use the locked vertex profile");
            check(std::find(args.begin(), args.end(), "-fspv-debug=line") != args.end(),
                  "Development mode must retain line debug information");
        }

        DiscoveredShaderToolchain toolchain;
        toolchain.manifest.identity = built.request->compiler_identity;
        toolchain.dxc_path = toy3d::PhysicalPath("locked-dxc");
        toolchain.spirv_val_path = toy3d::PhysicalPath("locked-spirv-val");
        std::size_t invocation_count = 0;
        const ShaderProcessRunner runner = [&](const toy3d::PhysicalPath&, const std::vector<std::string>& args)
        {
            toy3d::ProcessResult result;
            result.launched = true;
            result.exit_code = 0;
            if (invocation_count++ == 0)
            {
                const auto output = std::find(args.begin(), args.end(), "-Fo");
                if (output != args.end() && output + 1 != args.end())
                    write_bytes(*(output + 1), minimal_spirv_header());
            }
            return result;
        };
        const ShaderCompilerOutput compiled =
            compile_vulkan_shader(*built.request, toolchain, platform_file, physical_path(working), runner);
        check(compiled.succeeded(), "DXC adapter must publish binary only after spirv-val succeeds");
        check(invocation_count == 2, "successful Vulkan compile must invoke DXC and spirv-val exactly once");

        TargetBindingLayout empty_layout;
        empty_layout.target = ShaderTarget::VulkanSpirV;
        empty_layout.mapping_version = vulkan_binding_mapping_version;
        empty_layout.target_binding_hash[0] = 1u;
        const std::filesystem::path entry_root = working / "shader-code-entries";
        invocation_count = 0;
        const VulkanShaderCodeEntryResult reflection_failure =
            compile_vulkan_shader_code_entry(*built.request, empty_layout, toolchain, platform_file,
                                             physical_path(working / "reflection"), physical_path(entry_root), runner);
        check(!reflection_failure.succeeded() &&
                  has_diagnostic(reflection_failure.diagnostics, DiagnosticCode::ReflectionFailed),
              "invalid final SPIR-V must fail reflection before ShaderCodeEntry publication");
        check(!std::filesystem::exists(entry_root / toy3d::sha256_to_hex(built.request->compile_key)),
              "reflection failure must not publish a compile-key artifact directory");

        std::size_t failing_invocation = 0;
        const ShaderProcessRunner failing_validator =
            [&](const toy3d::PhysicalPath&, const std::vector<std::string>& args)
        {
            toy3d::ProcessResult result;
            result.launched = true;
            result.exit_code = failing_invocation++ == 0 ? 0 : 1;
            result.output = result.exit_code == 0 ? std::string{} : "validation failed";
            if (result.exit_code == 0)
            {
                const auto output = std::find(args.begin(), args.end(), "-Fo");
                if (output != args.end() && output + 1 != args.end())
                    write_bytes(*(output + 1), minimal_spirv_header());
            }
            return result;
        };
        const ShaderCompilerOutput validation_failure =
            compile_vulkan_shader(*built.request, toolchain, platform_file, physical_path(working), failing_validator);
        check(!validation_failure.binary &&
                  has_diagnostic(validation_failure.diagnostics, DiagnosticCode::ShaderValidationFailed),
              "spirv-val failure must suppress binary publication");

        const ShaderProcessRunner failing_compiler = [](const toy3d::PhysicalPath&, const std::vector<std::string>&)
        {
            return toy3d::ProcessResult{true, 1, "compile failed"};
        };
        const ShaderCompilerOutput compilation_failure =
            compile_vulkan_shader(*built.request, toolchain, platform_file, physical_path(working), failing_compiler);
        check(!compilation_failure.binary &&
                  has_diagnostic(compilation_failure.diagnostics, DiagnosticCode::ShaderCompilationFailed),
              "DXC failure must suppress binary publication");

        toolchain.manifest.identity = "wrong";
        const ShaderCompilerOutput identity_mismatch =
            compile_vulkan_shader(*built.request, toolchain, platform_file, physical_path(working), runner);
        check(!identity_mismatch.succeeded() &&
                  has_diagnostic(identity_mismatch.diagnostics, DiagnosticCode::CompilerUnavailable),
              "adapter must reject a mismatched compiler identity");
        std::filesystem::remove_all(working);
    }

    toy3d::shader::ShaderParameterSchema make_parameter_schema()
    {
        using namespace toy3d::shader;
        ShaderParameterSchema schema;
        ShaderParameterConstantBufferSchema buffer;
        buffer.binding_id = make_shader_parameter_id(BindingGroup::Pass, ShaderParameterCategory::Constant, "");
        buffer.name = "toy_pass_data";
        buffer.group = BindingGroup::Pass;
        buffer.size = 16u;
        ShaderParameterConstantMemberSchema member;
        member.parameter_id = make_shader_parameter_id(BindingGroup::Pass, ShaderParameterCategory::Constant, "tint");
        member.name = "tint";
        member.type = ShaderValueType::Float32x4;
        member.size = 16u;
        member.default_value.assign(16u, 0u);
        buffer.members.push_back(member);
        buffer.data_layout_hash = calculate_constant_buffer_data_layout_hash(
            buffer.group, buffer.binding_id, buffer.size,
            {{member.parameter_id, member.name, member.type, member.offset, member.size, member.array_stride,
              member.matrix_stride}});
        schema.constant_buffers.push_back(std::move(buffer));

        ShaderParameterResourceSchema texture;
        texture.parameter_id =
            make_shader_parameter_id(BindingGroup::Pass, ShaderParameterCategory::SampledTexture, "source_texture");
        texture.name = "source_texture";
        texture.group = BindingGroup::Pass;
        texture.category = ShaderParameterCategory::SampledTexture;
        texture.resource_kind = ResourceKind::Texture2D;
        texture.element_type = ShaderResourceElementType::Float4;
        texture.default_value_kind = ShaderParameterDefaultValueKind::Identifier;
        texture.default_value = "white";
        schema.resources.push_back(std::move(texture));
        schema.logical_layout_hash = calculate_shader_parameter_logical_layout_hash(schema);
        schema.schema_identity = calculate_shader_parameter_schema_identity(schema);
        return schema;
    }

    toy3d::shader::ShaderMapEntry make_shader_map_entry()
    {
        using namespace toy3d::shader;
        ShaderMapEntry entry;
        entry.shader_name = "Tests/Storage";
        entry.pass_name = "Forward";
        entry.mapping_version = vulkan_binding_mapping_version;
        entry.parameter_schema = make_parameter_schema();
        entry.logical_layout_hash = entry.parameter_schema.logical_layout_hash;
        entry.graphics_pass_state.cull_mode = ShaderGraphicsPassState::CullMode::Front;
        entry.pass_template_hash = calculate_shader_graphics_pass_state_hash(entry.graphics_pass_state);
        entry.permutation_key[0] = 6u;
        TargetBindingLayout empty_layout;
        empty_layout.target = ShaderTarget::VulkanSpirV;
        empty_layout.mapping_version = vulkan_binding_mapping_version;
        entry.target_binding_hash = calculate_target_binding_hash(empty_layout);

        ShaderCodeEntry stage;
        stage.request.stage = ShaderStageFlags::Vertex;
        stage.request.entry_point = "vs_main";
        stage.request.logical_layout_hash = entry.logical_layout_hash;
        stage.request.target_binding_hash = entry.target_binding_hash;
        stage.request.compile_key[0] = 4u;
        stage.reflection.stage = ShaderStageFlags::Vertex;
        stage.reflection.entry_point = "vs_main";
        stage.reflection.reflection_hash = calculate_shader_stage_reflection_hash(stage.reflection);
        stage.binary = minimal_spirv_header();
        entry.stages.push_back(std::move(stage));
        return entry;
    }

    void test_shader_map_storage_faults_and_concurrency()
    {
        using namespace toy3d::shader;
        const ShaderMapEntry entry = make_shader_map_entry();

        for (const FaultInjectingPlatformFile::Failure failure :
             {FaultInjectingPlatformFile::Failure::WriteText, FaultInjectingPlatformFile::Failure::Rename})
        {
            const std::filesystem::path root = make_test_directory("storage_fault");
            const std::filesystem::path entry_root = root / "entries";
            FaultInjectingPlatformFile faulting_file(failure);
            const ShaderMapEntryWriteResult written =
                write_verified_shader_map_entry(faulting_file, physical_path(entry_root), entry);
            check(!written.succeeded() && has_diagnostic(written.diagnostics, DiagnosticCode::ShaderCodeWriteFailed),
                  "injected ShaderMap storage failure must be diagnostic");
            const toy3d::FileResult<std::vector<toy3d::DirectoryEntry>> children =
                platform_file.enumerate_directory(physical_path(entry_root));
            check(children.succeeded() && children.value().empty(),
                  "failed ShaderMap publication must clean only its owned staging directory");
            std::filesystem::remove_all(root);
        }

        const std::filesystem::path root = make_test_directory("storage_concurrency");
        const toy3d::PhysicalPath entry_root = physical_path(root / "entries");
        ShaderMapEntryWriteResult first;
        ShaderMapEntryWriteResult second;
        std::thread first_thread([&] { first = write_verified_shader_map_entry(platform_file, entry_root, entry); });
        std::thread second_thread([&] { second = write_verified_shader_map_entry(platform_file, entry_root, entry); });
        first_thread.join();
        second_thread.join();
        check(first.succeeded() && second.succeeded() && first.cache_hit != second.cache_hit,
              "concurrent publication of one ShaderMap key must have one publisher and one cache hit");
        const toy3d::FileResult<std::vector<toy3d::DirectoryEntry>> children =
            platform_file.enumerate_directory(entry_root);
        check(children.succeeded() && children.value().size() == 1u &&
                  children.value()[0].path.utf8().find(".tmp.") == std::string::npos,
              "concurrent ShaderMap publication must leave one final directory and no staging residue");
        std::filesystem::remove_all(root);

        const std::filesystem::path reader_root = make_test_directory("storage_reader");
        const toy3d::PhysicalPath reader_entry_root = physical_path(reader_root / "entries");
        const ShaderMapEntryWriteResult published =
            write_verified_shader_map_entry(platform_file, reader_entry_root, entry);
        check(published.succeeded() && !published.cache_hit,
              "first ShaderMapEntry publication must create a new cache record");
        const ShaderMapEntryReadResult read =
            read_verified_shader_map_entry(platform_file, reader_entry_root, published.shader_map_key);
        check(read.succeeded() && read.entry && read.entry->shader_name == entry.shader_name &&
                  read.entry->stages.size() == entry.stages.size() &&
                  read.entry->parameter_schema.schema_identity == entry.parameter_schema.schema_identity &&
                  read.entry->parameter_schema.constant_buffers.size() == 1u &&
                  read.entry->parameter_schema.resources.size() == 1u &&
                  read.entry->graphics_pass_state.cull_mode == ShaderGraphicsPassState::CullMode::Front &&
                  read.entry_content_hash == published.entry_content_hash,
              "ShaderMapEntry reader must round-trip the complete parameter schema record");

        ShaderParameterSchema invalid_identity = entry.parameter_schema;
        invalid_identity.schema_identity[0] ^= 0xffu;
        std::string schema_error;
        check(!validate_shader_parameter_schema(invalid_identity, schema_error),
              "a damaged complete schema identity must fail before publication");
        ShaderMapBinding unknown_active;
        unknown_active.binding_id = 999u;
        unknown_active.name = "unknown_texture";
        unknown_active.group = BindingGroup::Pass;
        unknown_active.category = ShaderParameterCategory::SampledTexture;
        unknown_active.stages = ShaderStageFlags::Pixel;
        check(!validate_active_bindings_are_schema_subset(entry.parameter_schema, {unknown_active}, schema_error),
              "a Program active binding outside the complete schema must fail before RHI publication");

        ShaderMapEntry damaged_schema_entry = entry;
        damaged_schema_entry.parameter_schema.schema_identity[0] ^= 0xffu;
        const ShaderMapEntryWriteResult damaged_schema_publication = write_verified_shader_map_entry(
            platform_file, physical_path(reader_root / "damaged-schema"), damaged_schema_entry);
        check(!damaged_schema_publication.succeeded() && !damaged_schema_publication.entry_directory,
              "damaged schema identity must suppress ShaderMapEntry publication");

        ShaderMapEntry non_subset_entry = entry;
        non_subset_entry.bindings.push_back(unknown_active);
        const ShaderMapEntryWriteResult non_subset_publication = write_verified_shader_map_entry(
            platform_file, physical_path(reader_root / "non-subset"), non_subset_entry);
        check(!non_subset_publication.succeeded() && !non_subset_publication.entry_directory,
              "an active binding outside the complete schema must suppress ShaderMapEntry publication");
        const ShaderMapEntryWriteResult duplicate =
            write_verified_shader_map_entry(platform_file, reader_entry_root, entry);
        check(duplicate.succeeded() && duplicate.cache_hit &&
                  duplicate.entry_content_hash == published.entry_content_hash,
              "an existing identical ShaderMapEntry must return a deterministic cache hit");

        ShaderMapEntry conflicting = entry;
        ShaderDependency conflict_dependency;
        conflict_dependency.virtual_path = "/Engine/ShaderIncludes/Conflict.hlsli";
        conflict_dependency.content_hash = toy3d::sha256("different dependency");
        conflicting.stages[0].request.dependencies.push_back(conflict_dependency);
        const ShaderMapEntryWriteResult conflict =
            write_verified_shader_map_entry(platform_file, reader_entry_root, conflicting);
        check(!conflict.succeeded() && has_diagnostic(conflict.diagnostics, DiagnosticCode::ShaderMapCacheConflict),
              "the same ShaderMap key with different validated content must fail as a cache conflict");
        std::filesystem::remove_all(reader_root);

        const auto publish_corrupt_fixture = [&](const std::string& name)
        {
            const std::filesystem::path fixture_root = make_test_directory(name);
            const ShaderMapEntryWriteResult fixture =
                write_verified_shader_map_entry(platform_file, physical_path(fixture_root / "entries"), entry);
            check(fixture.succeeded(), "corrupt-input fixture publication must succeed");
            return std::make_pair(fixture_root, fixture);
        };

        auto binary_fixture = publish_corrupt_fixture("storage_corrupt_binary");
        if (binary_fixture.second.entry_directory)
        {
            const std::filesystem::path binary_path =
                std::filesystem::u8path(binary_fixture.second.entry_directory->utf8()) / "vertex.spv";
            std::vector<std::uint8_t> corrupt_binary = minimal_spirv_header();
            corrupt_binary[0] ^= 0xffu;
            write_bytes(binary_path, corrupt_binary);
            const ShaderMapEntryReadResult corrupt = read_verified_shader_map_entry(
                platform_file, physical_path(binary_fixture.first / "entries"), binary_fixture.second.shader_map_key);
            check(!corrupt.succeeded() && !corrupt.diagnostics.empty(),
                  "binary content hash corruption must be rejected by the ShaderMap reader");
        }
        std::filesystem::remove_all(binary_fixture.first);

        auto version_fixture = publish_corrupt_fixture("storage_corrupt_version");
        if (version_fixture.second.entry_directory)
        {
            const std::filesystem::path manifest_path =
                std::filesystem::u8path(version_fixture.second.entry_directory->utf8()) / "manifest.txt";
            const toy3d::FileResult<std::string> manifest = platform_file.read_text_utf8(physical_path(manifest_path));
            if (manifest.succeeded())
            {
                std::string changed = manifest.value();
                const std::string current_version =
                    "shader_map_entry_version=" + std::to_string(shader_map_entry_version);
                const std::size_t version = changed.find(current_version);
                if (version != std::string::npos)
                    changed.replace(version, current_version.size(), "shader_map_entry_version=999");
                write_text(manifest_path, changed);
            }
            const ShaderMapEntryReadResult corrupt = read_verified_shader_map_entry(
                platform_file, physical_path(version_fixture.first / "entries"), version_fixture.second.shader_map_key);
            check(!corrupt.succeeded() && !corrupt.diagnostics.empty(),
                  "unsupported ShaderMapEntry versions must be rejected");
        }
        std::filesystem::remove_all(version_fixture.first);

        auto schema_fixture = publish_corrupt_fixture("storage_corrupt_schema_identity");
        if (schema_fixture.second.entry_directory)
        {
            const std::filesystem::path schema_path =
                std::filesystem::u8path(schema_fixture.second.entry_directory->utf8()) / "schema.txt";
            const toy3d::FileResult<std::string> schema_text = platform_file.read_text_utf8(physical_path(schema_path));
            if (schema_text.succeeded())
            {
                std::string changed = schema_text.value();
                const std::string identity = toy3d::sha256_to_hex(entry.parameter_schema.schema_identity);
                const std::size_t position = changed.find(identity);
                if (position != std::string::npos)
                    changed[position] = changed[position] == '0' ? '1' : '0';
                write_text(schema_path, changed);
            }
            const ShaderMapEntryReadResult corrupt = read_verified_shader_map_entry(
                platform_file, physical_path(schema_fixture.first / "entries"), schema_fixture.second.shader_map_key);
            check(!corrupt.succeeded() && !corrupt.diagnostics.empty(),
                  "stored schema identity corruption must fail before runtime publication");
        }
        std::filesystem::remove_all(schema_fixture.first);

        auto target_fixture = publish_corrupt_fixture("storage_corrupt_target");
        if (target_fixture.second.entry_directory)
        {
            const std::filesystem::path manifest_path =
                std::filesystem::u8path(target_fixture.second.entry_directory->utf8()) / "manifest.txt";
            const toy3d::FileResult<std::string> manifest = platform_file.read_text_utf8(physical_path(manifest_path));
            if (manifest.succeeded())
            {
                std::string changed = manifest.value();
                const std::size_t target = changed.find("target=2");
                if (target != std::string::npos)
                    changed.replace(target, 8u, "target=0");
                write_text(manifest_path, changed);
            }
            const ShaderMapEntryReadResult corrupt = read_verified_shader_map_entry(
                platform_file, physical_path(target_fixture.first / "entries"), target_fixture.second.shader_map_key);
            check(!corrupt.succeeded() && !corrupt.diagnostics.empty(),
                  "target/profile mismatches must be rejected by the ShaderMap reader");
        }
        std::filesystem::remove_all(target_fixture.first);

        auto pass_state_fixture = publish_corrupt_fixture("storage_corrupt_pass_state");
        if (pass_state_fixture.second.entry_directory)
        {
            // filesystem keeps corruption-fixture path composition portable across
            // the Windows and Unix test configurations.
            const std::filesystem::path manifest_path =
                std::filesystem::u8path(pass_state_fixture.second.entry_directory->utf8()) / "manifest.txt";
            const toy3d::FileResult<std::string> manifest = platform_file.read_text_utf8(physical_path(manifest_path));
            if (manifest.succeeded())
            {
                std::string changed = manifest.value();
                const std::size_t cull = changed.find("pass_cull_mode=1");
                if (cull != std::string::npos)
                    changed.replace(cull, std::string("pass_cull_mode=1").size(), "pass_cull_mode=2");
                write_text(manifest_path, changed);
            }
            const ShaderMapEntryReadResult corrupt =
                read_verified_shader_map_entry(platform_file, physical_path(pass_state_fixture.first / "entries"),
                                               pass_state_fixture.second.shader_map_key);
            check(!corrupt.succeeded() && !corrupt.diagnostics.empty(),
                  "graphics Pass state must strictly match pass_template_hash");
        }
        std::filesystem::remove_all(pass_state_fixture.first);

        auto dependency_fixture = publish_corrupt_fixture("storage_corrupt_dependencies");
        if (dependency_fixture.second.entry_directory)
        {
            const std::filesystem::path dependency_path =
                std::filesystem::u8path(dependency_fixture.second.entry_directory->utf8()) / "vertex.dependencies.txt";
            write_text(dependency_path,
                       "/Engine/ShaderIncludes/Tampered.hlsli\t" + toy3d::sha256_to_hex(toy3d::sha256("tampered")) + "\n");
            const ShaderMapEntryReadResult corrupt =
                read_verified_shader_map_entry(platform_file, physical_path(dependency_fixture.first / "entries"),
                                               dependency_fixture.second.shader_map_key);
            check(!corrupt.succeeded() && !corrupt.diagnostics.empty(),
                  "dependency record corruption must be rejected by its file content hash");
        }
        std::filesystem::remove_all(dependency_fixture.first);

        auto oversized_fixture = publish_corrupt_fixture("storage_oversized_mapping");
        if (oversized_fixture.second.entry_directory)
        {
            const std::filesystem::path mapping_path =
                std::filesystem::u8path(oversized_fixture.second.entry_directory->utf8()) / "mapping.txt";
            write_text(mapping_path, std::string(4u * 1024u * 1024u + 1u, 'x'));
            const ShaderMapEntryReadResult corrupt =
                read_verified_shader_map_entry(platform_file, physical_path(oversized_fixture.first / "entries"),
                                               oversized_fixture.second.shader_map_key);
            check(!corrupt.succeeded() && !corrupt.diagnostics.empty(),
                  "oversized ShaderMap metadata must be rejected before whole-file parsing");
        }
        std::filesystem::remove_all(oversized_fixture.first);
    }

#if defined(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT) ||                                                                       \
    (defined(TOY3D_SHADER_TEST_DXC) && defined(TOY3D_SHADER_TEST_SPIRV_VAL))
    void test_real_dxc_spirv_integration()
    {
        using namespace toy3d::shader;
        DiscoveredShaderToolchain toolchain;
#if defined(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT)
        const ToolchainDiscoveryResult discovered =
            discover_shader_toolchain(platform_file, toy3d::PhysicalPath(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT));
        check(discovered.succeeded(), "formal locked toolchain bundle must pass manifest and artifact hash discovery");
        if (!discovered.toolchain)
            return;
        toolchain = *discovered.toolchain;
#else
        toolchain.manifest.identity = "Toy3dShaderToolchain/explicit-integration-test";
        toolchain.dxc_path = toy3d::PhysicalPath(TOY3D_SHADER_TEST_DXC);
        toolchain.spirv_val_path = toy3d::PhysicalPath(TOY3D_SHADER_TEST_SPIRV_VAL);
#endif
        const std::filesystem::path working = make_test_directory("real_dxc");
        TargetBindingLayout empty_layout;
        empty_layout.target = ShaderTarget::VulkanSpirV;
        empty_layout.mapping_version = vulkan_binding_mapping_version;
        empty_layout.target_binding_hash[0] = 1u;
        const RegisteredShaderSourceProvider empty_provider({});
        ShaderCompileRequestInput vertex = make_input(empty_provider);
        vertex.compiler_identity = toolchain.manifest.identity;
        vertex.shader_include_source.clear();
        vertex.pass_source =
            "float4 vs_main(uint vertex_id : SV_VertexID) : SV_Position { return float4(vertex_id == 1 ? "
            "1.0 : -1.0, vertex_id == 2 ? 1.0 : -1.0, 0.0, 1.0); }";
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
                *vertex_request.request, empty_layout, toolchain, platform_file, physical_path(working / "compile"),
                physical_path(working / "artifacts"));
            check(compiled.succeeded(),
                  "explicit DXC vertex output must pass reflection and publish a verified ShaderCodeEntry");
            check(
                compiled.entry_directory &&
                    std::filesystem::exists(std::filesystem::u8path(compiled.entry_directory->utf8()) /
                                            "manifest.txt") &&
                    std::filesystem::exists(std::filesystem::u8path(compiled.entry_directory->utf8()) / "shader.spv") &&
                    std::filesystem::exists(std::filesystem::u8path(compiled.entry_directory->utf8()) /
                                            "reflection.txt"),
                "verified vertex ShaderCodeEntry must contain manifest, binary, and reflection records");
        }
        if (pixel_request.request)
        {
            const VulkanShaderCodeEntryResult compiled = compile_vulkan_shader_code_entry(
                *pixel_request.request, empty_layout, toolchain, platform_file, physical_path(working / "compile"),
                physical_path(working / "artifacts"));
            check(compiled.succeeded(),
                  "explicit DXC pixel output must pass reflection and publish a verified ShaderCodeEntry");
        }

        ConstantBufferLayout material_constants;
        material_constants.group = BindingGroup::Material;
        material_constants.binding_id = 10u;
        material_constants.size = 112u;
        material_constants.members = {{11u, "tint", ShaderValueType::Float32x3, 0u, 12u, 1u, 0u, 0u, {}, {}},
                                      {12u, "factor", ShaderValueType::Float32, 12u, 4u, 1u, 0u, 0u, {}, {}},
                                      {13u, "transform", ShaderValueType::Float32x4x4, 16u, 64u, 1u, 0u, 16u, {}, {}},
                                      {14u, "weights", ShaderValueType::Float32x4, 80u, 32u, 2u, 16u, 0u, {}, {}}};
        const std::vector<ReflectedConstantMember> reflected_material_members = {
            {11u, "tint", ShaderValueType::Float32x3, 0u, 12u, 0u, 0u},
            {12u, "factor", ShaderValueType::Float32, 12u, 4u, 0u, 0u},
            {13u, "transform", ShaderValueType::Float32x4x4, 16u, 64u, 0u, 16u},
            {14u, "weights", ShaderValueType::Float32x4, 80u, 32u, 16u, 0u}};
        material_constants.data_layout_hash = calculate_constant_buffer_data_layout_hash(
            material_constants.group, material_constants.binding_id, material_constants.size,
            reflected_material_members);
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
             ShaderStageFlags::Pixel, NativeRegisterClass::ConstantBuffer, 0u, 2u, 0u, material_constants.size,
             material_constants.data_layout_hash, toy_shader_abi_version, &active_constants},
            {20u, "material_texture", BindingGroup::Material, ShaderParameterCategory::SampledTexture,
             ShaderStageFlags::Pixel, NativeRegisterClass::ShaderResource, 0u, 2u, 1u, 0u, {}, 0u,
             &active_texture},
            {21u, "material_sampler", BindingGroup::Material, ShaderParameterCategory::Sampler, ShaderStageFlags::Pixel,
             NativeRegisterClass::Sampler, 0u, 2u, 2u, 0u, {}, 0u, &active_sampler}};
        ShaderCompileRequestInput resource_pixel = pixel;
        resource_pixel.generated_bindings = "[[vk::binding(0, 2)]]\n"
                                            "cbuffer ToyMaterialConstants : register(b0)\n"
                                            "{\n"
                                            "    float3 tint : packoffset(c0);\n"
                                            "    float factor : packoffset(c0.w);\n"
                                            "    column_major float4x4 transform : packoffset(c1);\n"
                                            "    float4 weights[2] : packoffset(c5);\n"
                                            "};\n"
                                            "[[vk::binding(1, 2)]] Texture2D<float4> material_texture : register(t0);\n"
                                            "[[vk::binding(2, 2)]] SamplerState material_sampler : register(s0);\n";
        resource_pixel.pass_source = "float4 ps_main(float2 uv : TEXCOORD0) : SV_Target0 "
                                     "{ return material_texture.Sample(material_sampler, uv) * "
                                     "(mul(transform, float4(tint * factor, 1.0)) + weights[1]); }";
        resource_pixel.target_binding_hash = resource_layout.target_binding_hash;
        const ShaderCompileRequestResult resource_request = build_shader_compile_request(resource_pixel);
        check(resource_request.succeeded(), "resource reflection integration request must build");
        if (resource_request.request)
        {
            const VulkanShaderCodeEntryResult compiled = compile_vulkan_shader_code_entry(
                *resource_request.request, resource_layout, toolchain, platform_file,
                physical_path(working / "compile"), physical_path(working / "resource-artifacts"));
            check(compiled.succeeded(),
                  "SPIRV-Reflect must validate constant offsets, resource types, and Vulkan set/binding mapping");

            TargetBindingLayout mismatched_layout = resource_layout;
            mismatched_layout.bindings[1].descriptor_binding = 7u;
            const VulkanShaderCodeEntryResult mismatch = compile_vulkan_shader_code_entry(
                *resource_request.request, mismatched_layout, toolchain, platform_file,
                physical_path(working / "compile-mismatch"), physical_path(working / "mismatch-artifacts"));
            check(!mismatch.succeeded() &&
                      has_diagnostic(mismatch.diagnostics, DiagnosticCode::ReflectionUnexpectedResource),
                  "native set/binding mismatch must fail parity validation");
            check(!std::filesystem::exists(working / "mismatch-artifacts" /
                                           toy3d::sha256_to_hex(resource_request.request->compile_key)),
                  "parity mismatch must not publish a ShaderCodeEntry");
        }
        std::filesystem::remove_all(working);
    }

#if defined(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT)
    void test_real_program_compiler()
    {
        using namespace toy3d::shader;
        const ToolchainDiscoveryResult discovered =
            discover_shader_toolchain(platform_file, toy3d::PhysicalPath(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT));
        check(discovered.succeeded(), "Program compiler requires the locked toolchain bundle");
        if (!discovered.toolchain)
            return;

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
    Variants
    {
        USE_TINT : bool = false
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
#if TOY3D_VARIANT_USE_TINT
            return source_texture.Sample(source_sampler, input.uv) * tint;
#else
            return source_texture.Sample(source_sampler, input.uv) * tint;
#endif
        }
        ENDHLSL
    }
})";
        const ParseResult parsed = parse_shader(source, "/Engine/Shaders/Tests/ProgramCompile.shader");
        check(parsed.succeeded(), "Program compiler test Shader must parse");
        if (!parsed.asset)
            return;

        ShaderProgramCompileInput input;
        input.pass_name = "Forward";
        input.source_virtual_path = "/Engine/Shaders/Tests/ProgramCompile.shader";
        input.variant_selections.push_back({"USE_TINT", "true"});
        const RegisteredShaderSourceProvider source_provider({});
        input.source_provider = &source_provider;
        const std::filesystem::path working = make_test_directory("program_compile");
        const ShaderMapEntryCompileResult compiled = compile_vulkan_shader_map_entry(
            *parsed.asset, input, *discovered.toolchain, platform_file, physical_path(working));
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
            check(compiled.entry->variant_id_version == shader_variant_id_version &&
                      compiled.entry->permutation_version == shader_permutation_version &&
                      std::any_of(compiled.entry->permutation_key.begin(), compiled.entry->permutation_key.end(),
                                  [](std::uint8_t byte) { return byte != 0u; }),
                  "ShaderMapEntry must persist its versioned typed permutation identity");
            check(compiled.entry->bindings.size() == 3u,
                  "Only the active Material cbuffer, texture, and sampler must remain");
            check(std::all_of(compiled.entry->bindings.begin(), compiled.entry->bindings.end(),
                              [](const ShaderMapBinding& binding)
                              { return binding.stages == ShaderStageFlags::Pixel; }),
                  "Discovery must compute final pixel-only stage visibility");
            check(compiled.entry->bindings[0].descriptor_set == 2u &&
                      compiled.entry->bindings[0].descriptor_binding == 0u &&
                      compiled.entry->bindings[1].descriptor_binding == 1u &&
                      compiled.entry->bindings[2].descriptor_binding == 2u,
                  "Final Vulkan Material bindings must be compact and deterministic");
            const ShaderMapEntryWriteResult entry_write =
                write_verified_shader_map_entry(platform_file, physical_path(working / "shader-map"), *compiled.entry);
            check(entry_write.succeeded(),
                  "Strictly validated Program compilation must publish one atomic ShaderMapEntry");
            check(
                entry_write.entry_directory &&
                    std::filesystem::exists(std::filesystem::u8path(entry_write.entry_directory->utf8()) /
                                            "manifest.txt") &&
                    std::filesystem::exists(std::filesystem::u8path(entry_write.entry_directory->utf8()) /
                                            "mapping.txt") &&
                    std::filesystem::exists(std::filesystem::u8path(entry_write.entry_directory->utf8()) /
                                            "vertex.spv") &&
                    std::filesystem::exists(std::filesystem::u8path(entry_write.entry_directory->utf8()) / "pixel.spv"),
                "ShaderMapEntry must contain manifest, mapping, and all stage binaries");
            const ShaderMapEntryWriteResult duplicate =
                write_verified_shader_map_entry(platform_file, physical_path(working / "shader-map"), *compiled.entry);
            check(duplicate.succeeded() && duplicate.cache_hit,
                  "An existing validated ShaderMapEntry must return a deterministic cache hit");
            const ShaderMapEntryReadResult loaded = read_verified_shader_map_entry(
                platform_file, physical_path(working / "shader-map"), entry_write.shader_map_key);
            check(loaded.succeeded() && loaded.entry && loaded.entry->bindings.size() == 3u &&
                      loaded.entry->stages.size() == 2u,
                  "ShaderMap reader must validate and reconstruct a real reflected Program Entry");
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
} // namespace

int main()
{
    test_variant_permutation_contract();
    test_include_resolution_and_compile_key();
    test_include_failures();
    test_request_validation();
    test_toolchain_discovery();
    test_toolchain_relative_root();
    test_dxc_arguments_and_adapter_flow();
    test_shader_map_storage_faults_and_concurrency();
#if defined(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT) ||                                                                       \
    (defined(TOY3D_SHADER_TEST_DXC) && defined(TOY3D_SHADER_TEST_SPIRV_VAL))
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
