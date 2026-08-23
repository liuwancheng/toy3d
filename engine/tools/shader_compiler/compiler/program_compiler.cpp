#include "compiler/program_compiler.h"

#include "codegen/binding_codegen.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        struct StageCompileOutput
        {
            // A stage is published only after its compile and reflection both
            // succeed; optional prevents partial multi-stage assembly.
            std::optional<ShaderCodeEntry> stage;
            std::vector<Diagnostic> diagnostics;
        };

        ShaderStageFlags stage_flag(ShaderStage stage)
        {
            switch (stage)
            {
            case ShaderStage::Vertex: return ShaderStageFlags::Vertex;
            case ShaderStage::Pixel: return ShaderStageFlags::Pixel;
            case ShaderStage::Compute: return ShaderStageFlags::Compute;
            }
            return ShaderStageFlags::None;
        }

        const char* stage_directory_name(ShaderStageFlags stage)
        {
            switch (stage)
            {
            case ShaderStageFlags::Vertex: return "vertex";
            case ShaderStageFlags::Pixel: return "pixel";
            case ShaderStageFlags::Compute: return "compute";
            default: return "unknown";
            }
        }

        std::string combined_include_source(const ShaderAsset& asset)
        {
            std::ostringstream source;
            for (const HlslBlock& include : asset.includes)
            {
                source << include.source << '\n';
            }
            return source.str();
        }

        std::vector<ParameterUsage> all_parameter_usage(
            const LogicalShaderLayout& logical_layout,
            ShaderStageFlags program_stages)
        {
            std::vector<ParameterUsage> usage;
            for (const ConstantBufferLayout& buffer : logical_layout.constant_buffers)
            {
                for (const ShaderConstantMember& member : buffer.members)
                {
                    usage.push_back({member.name, program_stages});
                }
            }
            for (const ShaderResourceParameter& resource : logical_layout.resources)
            {
                usage.push_back({resource.name, program_stages});
            }
            return usage;
        }

        StageCompileOutput compile_stage(
            const ShaderProgramCompileInput& input,
            const ShaderPermutation& permutation,
            const ShaderPass& pass,
            const EntryPoint& entry,
            const std::string& shader_include_source,
            const LogicalShaderLayout& logical_layout,
            const TargetBindingLayout& target_layout,
            const DiscoveredShaderToolchain& toolchain,
            PlatformFile& platform_file,
            const PhysicalPath& working_directory,
            bool require_all_expected_bindings,
            const ShaderProcessRunner& process_runner)
        {
            StageCompileOutput output;
            const ShaderStageFlags stage = stage_flag(entry.stage);
            BindingCodegenResult bindings = generate_binding_hlsl(logical_layout, target_layout, stage);
            if (!bindings.succeeded())
            {
                output.diagnostics = std::move(bindings.diagnostics);
                return output;
            }

            ShaderCompileRequestInput request_input;
            request_input.target = ShaderTarget::VulkanSpirV;
            request_input.profile = ShaderCompileProfile::VulkanPortableV1;
            request_input.stage = stage;
            request_input.debug_mode = input.debug_mode;
            request_input.entry_point = entry.name;
            request_input.source_virtual_path = input.source_virtual_path;
            request_input.compiler_identity = toolchain.manifest.identity;
            request_input.generated_prelude = permutation.generated_prelude;
            request_input.generated_bindings = std::move(*bindings.source);
            request_input.shader_include_source = shader_include_source;
            request_input.pass_source = pass.program.source;
            request_input.source_provider = input.source_provider;
            request_input.logical_layout_hash = logical_layout.logical_layout_hash;
            request_input.target_binding_hash = target_layout.target_binding_hash;
            ShaderCompileRequestResult request = build_shader_compile_request(request_input);
            if (!request.succeeded())
            {
                output.diagnostics = std::move(request.diagnostics);
                return output;
            }

            ShaderCompilerOutput compiled = compile_vulkan_shader(
                *request.request, toolchain, platform_file, working_directory, process_runner);
            if (!compiled.succeeded())
            {
                output.diagnostics = std::move(compiled.diagnostics);
                return output;
            }
            SpirvReflectionResult reflected = reflect_and_validate_spirv(
                *compiled.binary, *request.request, target_layout, require_all_expected_bindings);
            if (!reflected.succeeded())
            {
                output.diagnostics = std::move(reflected.diagnostics);
                return output;
            }
            output.stage = ShaderCodeEntry{
                std::move(*request.request), std::move(*reflected.reflection), std::move(*compiled.binary)};
            return output;
        }

        void append_diagnostics(std::vector<Diagnostic>& destination, std::vector<Diagnostic>&& source)
        {
            destination.insert(destination.end(),
                std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
        }

        void add_reflected_usage(
            std::vector<ParameterUsage>& usage,
            const ShaderStageReflection& reflection)
        {
            for (const ReflectedBinding& binding : reflection.bindings)
            {
                if (binding.category == ShaderParameterCategory::Constant)
                {
                    for (const ReflectedConstantMember& member : binding.constant_members)
                    {
                        usage.push_back({member.name, reflection.stage});
                    }
                }
                else
                {
                    usage.push_back({binding.name, reflection.stage});
                }
            }
        }

        bool validate_program_interfaces(
            const std::vector<ShaderCodeEntry>& stages,
            const SourceLocation& location,
            std::vector<Diagnostic>& diagnostics)
        {
            const auto vertex = std::find_if(stages.begin(), stages.end(), [](const auto& stage) {
                return stage.reflection.stage == ShaderStageFlags::Vertex;
            });
            const auto pixel = std::find_if(stages.begin(), stages.end(), [](const auto& stage) {
                return stage.reflection.stage == ShaderStageFlags::Pixel;
            });
            if (vertex != stages.end() && pixel != stages.end())
            {
                for (const ReflectedInterfaceVariable& input : pixel->reflection.interface_variables)
                {
                    if (!input.input) continue;
                    const auto output = std::find_if(
                        vertex->reflection.interface_variables.begin(),
                        vertex->reflection.interface_variables.end(),
                        [&](const ReflectedInterfaceVariable& candidate) {
                            return !candidate.input && candidate.location == input.location;
                        });
                    if (output == vertex->reflection.interface_variables.end() ||
                        (!input.semantic.empty() && !output->semantic.empty() &&
                            input.semantic != output->semantic) ||
                        (output != vertex->reflection.interface_variables.end() &&
                            (input.scalar_type != output->scalar_type ||
                             input.component_count != output->component_count)))
                    {
                        diagnostics.push_back({DiagnosticSeverity::Error,
                            DiagnosticCode::ReflectionMismatch, location,
                            "Pixel input at location " + std::to_string(input.location) +
                                " does not match a vertex output."});
                    }
                }
            }
            const auto compute = std::find_if(stages.begin(), stages.end(), [](const auto& stage) {
                return stage.reflection.stage == ShaderStageFlags::Compute;
            });
            if (compute != stages.end() &&
                (compute->reflection.thread_group_size_x == 0u ||
                 compute->reflection.thread_group_size_y == 0u ||
                 compute->reflection.thread_group_size_z == 0u))
            {
                diagnostics.push_back({DiagnosticSeverity::Error,
                    DiagnosticCode::ReflectionMismatch, location,
                    "Compute Program reflection requires a non-zero thread-group size."});
            }
            return diagnostics.empty();
        }
    }

    bool ShaderMapEntryCompileResult::succeeded() const
    {
        return entry.has_value() && diagnostics.empty();
    }

    ShaderMapEntryCompileResult compile_vulkan_shader_map_entry(
        const ShaderAsset& asset,
        const ShaderProgramCompileInput& input,
        const DiscoveredShaderToolchain& toolchain,
        PlatformFile& platform_file,
        const PhysicalPath& working_directory,
        const ShaderProcessRunner& process_runner)
    {
        ShaderMapEntryCompileResult result;
        const auto pass = std::find_if(asset.passes.begin(), asset.passes.end(), [&](const ShaderPass& candidate) {
            return candidate.name == input.pass_name;
        });
        if (pass == asset.passes.end() || input.source_virtual_path.empty() ||
            input.source_provider == nullptr ||
            pass->program.entry_points.empty())
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error,
                DiagnosticCode::InvalidCompileRequest, asset.location,
                "Program compilation requires an existing Pass, entry points, and a virtual source path."});
            return result;
        }
        ShaderPermutationResult permutation =
            resolve_shader_permutation(asset, input.variant_selections);
        if (!permutation.succeeded())
        {
            result.diagnostics = std::move(permutation.diagnostics);
            return result;
        }

        LogicalLayoutResult logical = compile_logical_layout(asset);
        if (!logical.succeeded())
        {
            result.diagnostics = std::move(logical.diagnostics);
            return result;
        }
        ShaderStageFlags program_stages = ShaderStageFlags::None;
        for (const EntryPoint& entry : pass->program.entry_points) program_stages |= stage_flag(entry.stage);
        ActiveLayoutResult discovery_active = build_active_layout(
            *logical.layout, all_parameter_usage(*logical.layout, program_stages));
        if (!discovery_active.succeeded())
        {
            result.diagnostics = std::move(discovery_active.diagnostics);
            return result;
        }
        TargetBindingResult discovery_mapping = allocate_target_bindings(
            *discovery_active.layout, ShaderTarget::VulkanSpirV,
            TargetBindingLimits::vulkan_portable_v1());
        if (!discovery_mapping.succeeded())
        {
            result.diagnostics = std::move(discovery_mapping.diagnostics);
            return result;
        }

        const std::string shader_include_source = combined_include_source(asset);
        const FileResult<PhysicalPath> discovery_root =
            platform_file.join_relative(working_directory, "discovery");
        const FileResult<PhysicalPath> final_root =
            platform_file.join_relative(working_directory, "final");
        if (!discovery_root.succeeded() || !final_root.succeeded())
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error,
                DiagnosticCode::ShaderCompilationFailed, asset.location,
                "Unable to resolve Shader compiler working directories."});
            return result;
        }
        std::vector<ParameterUsage> reflected_usage;
        for (const EntryPoint& entry : pass->program.entry_points)
        {
            const FileResult<PhysicalPath> stage_working_directory =
                platform_file.join_relative(
                    discovery_root.value(),
                    stage_directory_name(stage_flag(entry.stage)));
            if (!stage_working_directory.succeeded())
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error,
                    DiagnosticCode::ShaderCompilationFailed, entry.location,
                    "Unable to resolve discovery compile working directory."});
                return result;
            }
            StageCompileOutput discovered = compile_stage(
                input, *permutation.permutation, *pass, entry, shader_include_source, *logical.layout,
                *discovery_mapping.layout, toolchain, platform_file,
                stage_working_directory.value(),
                false, process_runner);
            if (!discovered.stage)
            {
                append_diagnostics(result.diagnostics, std::move(discovered.diagnostics));
                return result;
            }
            add_reflected_usage(reflected_usage, discovered.stage->reflection);
        }

        ActiveLayoutResult final_active = build_active_layout(*logical.layout, reflected_usage);
        if (!final_active.succeeded())
        {
            result.diagnostics = std::move(final_active.diagnostics);
            return result;
        }
        TargetBindingResult final_mapping = allocate_target_bindings(
            *final_active.layout, ShaderTarget::VulkanSpirV,
            TargetBindingLimits::vulkan_portable_v1());
        if (!final_mapping.succeeded())
        {
            result.diagnostics = std::move(final_mapping.diagnostics);
            return result;
        }

        ShaderMapEntry entry;
        entry.shader_name = asset.name;
        entry.pass_name = pass->name;
        entry.target = ShaderTarget::VulkanSpirV;
        entry.profile = ShaderCompileProfile::VulkanPortableV1;
        entry.logical_layout_hash = logical.layout->logical_layout_hash;
        entry.target_binding_hash = final_mapping.layout->target_binding_hash;
        entry.graphics_pass_state = pass->state;
        entry.pass_template_hash =
            calculate_shader_graphics_pass_state_hash(entry.graphics_pass_state);
        entry.variant_id_version = permutation.permutation->variant_id_version;
        entry.permutation_version = permutation.permutation->version;
        entry.permutation_key = permutation.permutation->key;
        entry.mapping_version = final_mapping.layout->mapping_version;
        for (const NativeBinding& binding : final_mapping.layout->bindings)
        {
            entry.bindings.push_back({binding.binding_id, binding.name, binding.group,
                binding.category, binding.stages, binding.register_class, binding.register_index,
                binding.descriptor_set, binding.descriptor_binding});
        }
        for (const EntryPoint& entry_point : pass->program.entry_points)
        {
            const FileResult<PhysicalPath> stage_working_directory =
                platform_file.join_relative(
                    final_root.value(),
                    stage_directory_name(stage_flag(entry_point.stage)));
            if (!stage_working_directory.succeeded())
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error,
                    DiagnosticCode::ShaderCompilationFailed, entry_point.location,
                    "Unable to resolve final compile working directory."});
                return result;
            }
            StageCompileOutput compiled = compile_stage(
                input, *permutation.permutation, *pass, entry_point, shader_include_source, *logical.layout,
                *final_mapping.layout, toolchain, platform_file,
                stage_working_directory.value(),
                true, process_runner);
            if (!compiled.stage)
            {
                append_diagnostics(result.diagnostics, std::move(compiled.diagnostics));
                return result;
            }
            entry.stages.push_back(std::move(*compiled.stage));
        }
        if (!validate_program_interfaces(entry.stages, pass->location, result.diagnostics))
        {
            return result;
        }
        result.entry = std::move(entry);
        return result;
    }
}
