#include "compiler/program_compiler.h"

#include "shader/builtin_shader_parameters.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>

#include <spirv_reflect.h>

#include "codegen/binding_codegen.h"
#include "compiler/standard_surface.h"
#include "frontend/tokenizer.h"
#include <set>

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
            case ShaderStage::Vertex:
                return ShaderStageFlags::Vertex;
            case ShaderStage::Pixel:
                return ShaderStageFlags::Pixel;
            case ShaderStage::Compute:
                return ShaderStageFlags::Compute;
            }
            return ShaderStageFlags::None;
        }

        const char* stage_directory_name(ShaderStageFlags stage)
        {
            switch (stage)
            {
            case ShaderStageFlags::Vertex:
                return "vertex";
            case ShaderStageFlags::Pixel:
                return "pixel";
            case ShaderStageFlags::Compute:
                return "compute";
            default:
                return "unknown";
            }
        }

        std::string combined_include_source(const ShaderAsset& asset)
        {
            std::ostringstream source;
            if (asset.geometry == ShaderGeometryMode::Standard)
            {
                source << "#include \"/Engine/ShaderIncludes/ToySurface.hlsli\"\n";
            }
            for (const HlslBlock& include : asset.includes)
            {
                source << "#line " << include.location.line << " \"" << asset.location.path << "\"\n";
                source << include.source << '\n';
            }
            return source.str();
        }

        std::vector<ParameterUsage> all_parameter_usage(const LogicalShaderLayout& logical_layout,
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

        std::set<std::string> identifiers(const std::string& source, const std::string& path)
        {
            std::set<std::string> names;
            Tokenizer tokenizer(source, path);
            for (Token token = tokenizer.next(); token.kind != TokenKind::EndOfFile; token = tokenizer.next())
            {
                if (token.kind == TokenKind::Identifier)
                {
                    names.insert(token.text);
                }
            }
            return names;
        }

        bool validate_static_macro_directives(const std::string& source, const std::string& path,
                                              std::vector<Diagnostic>& diagnostics)
        {
            // Tokenizer skips comments and strings. Reject token concatenation,
            // which can synthesize an identifier outside the bounded dependency inventory.
            Tokenizer tokenizer(source, path);
            Token previous;
            bool directive_name = false;
            std::size_t directive_line = 0u;
            for (Token token = tokenizer.next(); token.kind != TokenKind::EndOfFile; token = tokenizer.next())
            {
                if (token.text == "#" && previous.text == "#" && token.location.line == previous.location.line)
                {
                    diagnostics.push_back(
                        {DiagnosticSeverity::Error, DiagnosticCode::InvalidVariant, token.location,
                         "Token concatenation is unsupported in the static macro dependency contract."});
                    return false;
                }
                if (directive_name && token.location.line == directive_line)
                {
                    if (token.text.compare(0u, 14u, "TOY3D_VARIANT_") == 0 ||
                        token.text.compare(0u, 11u, "TOY3D_PASS_") == 0 || token.text == "TOY3D_GPU_SKIN")
                    {
                        diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidVariant,
                                               token.location,
                                               "Static and VertexFactory macros are owned by the compiler."});
                        return false;
                    }
                    directive_name = false;
                }
                if (previous.text == "#" && token.location.line == previous.location.line &&
                    (token.text == "define" || token.text == "undef"))
                {
                    directive_name = true;
                    directive_line = token.location.line;
                }
                previous = std::move(token);
            }
            return true;
        }

        void append_used_defines(std::string& prelude, const std::string& definitions,
                                 const std::set<std::string>& names)
        {
            std::istringstream lines(definitions);
            std::string line;
            while (std::getline(lines, line))
            {
                std::istringstream fields(line);
                std::string directive, name;
                fields >> directive >> name;
                if (directive == "#define" && names.count(name) != 0u)
                {
                    prelude += line + "\n";
                }
            }
        }

        StageCompileOutput compile_stage(const ShaderProgramCompileInput& input, const ShaderPermutation& permutation,
                                         const ShaderPermutationDomain& material_domain,
                                         const ShaderPermutationDomain& engine_domain,
                                         const std::vector<ShaderEngineFeatureDeclaration>& feature_declarations,
                                         const ShaderPass& pass, const EntryPoint& entry,
                                         const std::string& shader_include_source,
                                         const LogicalShaderLayout& logical_layout,
                                         const TargetBindingLayout& target_layout,
                                         const DiscoveredShaderToolchain& toolchain, PlatformFile& platform_file,
                                         const PhysicalPath& working_directory, bool require_all_expected_bindings,
                                         const ShaderProcessRunner& process_runner)
        {
            StageCompileOutput output;
            const ShaderStageFlags stage = stage_flag(entry.stage);
            const auto program = std::find_if(pass.programs.begin(), pass.programs.end(),
                                              [&](const HlslBlock& block)
                                              {
                                                  return block.entry_points.size() == 1u &&
                                                         block.entry_points.front().stage == entry.stage;
                                              });
            if (program == pass.programs.end())
            {
                output.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::MissingEntryPoint,
                                              pass.location, "Missing stage source block."});
                return output;
            }
            std::ostringstream body;
            body << shader_include_source << "\n#line " << program->location.line << " \"" << input.source_virtual_path
                 << "\"\n"
                 << program->source;
            auto expanded = resolve_shader_includes(body.str(), input.source_virtual_path, *input.source_provider);
            if (!expanded.succeeded())
            {
                output.diagnostics = std::move(expanded.diagnostics);
                return output;
            }
            if (!validate_static_macro_directives(*expanded.source, input.source_virtual_path, output.diagnostics))
            {
                return output;
            }
            const auto names = identifiers(*expanded.source, input.source_virtual_path);
            if (pass.role == ShaderPassRole::Forward)
            {
                const auto declared = [&](ShaderEngineFeature feature)
                {
                    return std::any_of(feature_declarations.begin(), feature_declarations.end(),
                                       [feature](const ShaderEngineFeatureDeclaration& value)
                                       {
                                           return value.feature == feature;
                                       });
                };
                const auto require = [&](const std::string& name, ShaderEngineFeature feature)
                {
                    if (names.count(name) != 0u && !declared(feature))
                    {
                        output.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidParameterGroup,
                                                      program->location,
                                                      "Engine Forward input requires a declared Feature: " + name});
                    }
                };
                for (const auto& parameter : builtin_forward_parameters)
                {
                    const std::string name = parameter.name;
                    const auto feature = name.compare(0u, 12u, "environment_") == 0
                                             ? ShaderEngineFeature::Environment
                                             : (name.compare(0u, 7u, "shadow_") == 0 ? ShaderEngineFeature::Shadows
                                                                                     : ShaderEngineFeature::Lighting);
                    require(name, feature);
                }
                for (const auto& resource : builtin_forward_resources)
                {
                    const std::string name = resource.name;
                    require(name, name.compare(0u, 12u, "environment_") == 0 ? ShaderEngineFeature::Environment
                                                                             : ShaderEngineFeature::Shadows);
                }
                for (const auto& name : names)
                {
                    if (name.rfind("TOY3D_PASS_SHADOW_MODE", 0u) == 0)
                    {
                        require(name, ShaderEngineFeature::Shadows);
                    }
                    if (name.rfind("TOY3D_PASS_ENVIRONMENT_MODE", 0u) == 0)
                    {
                        require(name, ShaderEngineFeature::Environment);
                    }
                }
            }
            std::set<std::string> known;
            const auto check_domain = [&](const ShaderPermutationDomain& domain)
            {
                for (const auto& dimension : domain.dimensions)
                {
                    ShaderPermutationDomain single;
                    single.scope = domain.scope;
                    single.dimensions.push_back(dimension);
                    const auto definitions = resolve_shader_permutation(single, {});
                    const auto defined = identifiers(definitions.permutation->generated_prelude, "");
                    for (const auto& name : defined)
                    {
                        if (name.compare(0u, 14u, "TOY3D_VARIANT_") == 0 || name.compare(0u, 11u, "TOY3D_PASS_") == 0)
                        {
                            known.insert(name);
                            if (names.count(name) != 0u &&
                                (!has_stage(dimension.affected_stages, stage) ||
                                 (dimension.affected_passes & shader_pass_role_bit(pass.role)) == 0u))
                            {
                                output.diagnostics.push_back(
                                    {DiagnosticSeverity::Error, DiagnosticCode::InvalidVariant, program->location,
                                     "Macro " + name + " is used outside its declared stage/Pass impact."});
                            }
                        }
                    }
                }
            };
            check_domain(material_domain);
            ShaderEngineFeatures all_features;
            all_features.lighting = all_features.shadows = all_features.environment = true;
            check_domain(shader_pass_domain(ShaderPassRole::Forward, all_features));
            for (const auto& name : names)
            {
                if ((name.compare(0u, 14u, "TOY3D_VARIANT_") == 0 || name.compare(0u, 11u, "TOY3D_PASS_") == 0) &&
                    known.count(name) == 0u)
                {
                    output.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidVariant,
                                                  program->location, "Undeclared permutation macro: " + name});
                }
            }
            if (!output.diagnostics.empty())
            {
                return output;
            }
            BindingCodegenResult bindings = generate_binding_hlsl(logical_layout, target_layout, stage, &names);
            if (!bindings.succeeded())
            {
                output.diagnostics = std::move(bindings.diagnostics);
                return output;
            }
            std::vector<ShaderMapBinding> stage_bindings;
            for (const auto& binding : target_layout.bindings)
            {
                if (has_stage(binding.stages, stage) && binding.logical_binding != nullptr)
                {
                    bool used = names.count(binding.name) != 0u;
                    if (binding.logical_binding->constant_buffer != nullptr)
                    {
                        for (const auto& member : binding.logical_binding->constant_buffer->members)
                        {
                            used = used || names.count(member.name) != 0u;
                        }
                    }
                    if (used)
                    {
                        stage_bindings.push_back({binding.binding_id, binding.name, binding.group, binding.category,
                                                  stage, binding.register_class, binding.register_index,
                                                  binding.descriptor_set, binding.descriptor_binding, binding.data_size,
                                                  binding.data_layout_hash, binding.shader_abi_version});
                    }
                }
            }
            ShaderCompileRequestInput request_input;
            request_input.target = ShaderTarget::VulkanSpirV;
            request_input.profile = ShaderCompileProfile::VulkanES31;
            request_input.stage = stage;
            request_input.debug_mode = input.debug_mode;
            request_input.entry_point = entry.name;
            request_input.source_virtual_path = input.source_virtual_path;
            request_input.compiler_identity = toolchain.manifest.identity;
            const auto material = resolve_shader_permutation(material_domain, permutation.selections, stage, pass.role);
            const auto engine = resolve_shader_permutation(engine_domain, input.pass_selections, stage, pass.role);
            append_used_defines(request_input.generated_prelude, material.permutation->generated_prelude, names);
            append_used_defines(request_input.generated_prelude, engine.permutation->generated_prelude, names);
            if (names.count("TOY3D_GPU_SKIN") != 0u)
            {
                request_input.generated_prelude += input.vertex_factory == VertexFactoryType::GPUSkin
                                                       ? "#define TOY3D_GPU_SKIN 1\n"
                                                       : "#define TOY3D_GPU_SKIN 0\n";
            }
            request_input.generated_bindings = std::move(*bindings.source);
            request_input.pass_source = std::move(*expanded.source);
            request_input.source_dependencies = std::move(expanded.dependencies);
            request_input.source_provider = input.source_provider;
            request_input.logical_layout_hash = calculate_shader_stage_logical_layout_hash(
                make_shader_parameter_schema(logical_layout), stage_bindings, stage);
            request_input.target_binding_hash = calculate_shader_stage_binding_hash(
                target_layout.target, target_layout.mapping_version, stage_bindings, stage);
            ShaderCompileRequestResult request = build_shader_compile_request(request_input);
            if (!request.succeeded())
            {
                output.diagnostics = std::move(request.diagnostics);
                return output;
            }

            ShaderCompilerOutput compiled;
            const std::size_t stage_index =
                stage == ShaderStageFlags::Vertex ? 0u : (stage == ShaderStageFlags::Pixel ? 1u : 2u);
            const auto cached = input.stage_cache == nullptr ? nullptr : &input.stage_cache->binaries;
            if (cached != nullptr && cached->count(request.request->compile_key) != 0u)
            {
                compiled.binary = cached->at(request.request->compile_key);
                ++input.stage_cache->reused[stage_index];
            }
            else
            {
                compiled = compile_vulkan_shader(*request.request, toolchain, platform_file, working_directory,
                                                 process_runner);
                if (compiled.succeeded() && input.stage_cache != nullptr)
                {
                    input.stage_cache->binaries.emplace(request.request->compile_key, *compiled.binary);
                    ++input.stage_cache->compiled[stage_index];
                }
            }
            if (!compiled.succeeded())
            {
                output.diagnostics = std::move(compiled.diagnostics);
                return output;
            }
            SpirvReflectionResult reflected = reflect_and_validate_spirv(*compiled.binary, *request.request,
                                                                         target_layout, require_all_expected_bindings);
            if (!reflected.succeeded())
            {
                output.diagnostics = std::move(reflected.diagnostics);
                return output;
            }
            output.stage = ShaderCodeEntry{std::move(*request.request), std::move(*reflected.reflection),
                                           std::move(*compiled.binary)};
            return output;
        }

        void append_diagnostics(std::vector<Diagnostic>& destination, std::vector<Diagnostic>&& source)
        {
            destination.insert(destination.end(), std::make_move_iterator(source.begin()),
                               std::make_move_iterator(source.end()));
        }

        bool validate_standard_fragment(const ShaderCodeEntry& stage, const SourceLocation& location, bool coverage,
                                        std::vector<Diagnostic>& diagnostics)
        {
            // SPIR-V is already validated by spirv-val. Inspect reachable user
            // bytecode in a wrapper without the compiler's own coverage clip.
            const auto& binary = stage.binary;
            if (binary.size() < 5u * sizeof(std::uint32_t) || binary.size() % sizeof(std::uint32_t) != 0u)
            {
                diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ReflectionMismatch, location,
                                       "Standard fragment validation requires complete SPIR-V."});
                return false;
            }
            std::vector<std::uint32_t> words(binary.size() / sizeof(std::uint32_t));
            std::memcpy(words.data(), binary.data(), binary.size());
            for (std::size_t offset = 5u; offset < words.size();)
            {
                const auto count = words[offset] >> 16u;
                const auto opcode = words[offset] & 0xffffu;
                if (count == 0u || count > words.size() - offset)
                {
                    diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ReflectionMismatch, location,
                                           "Malformed Standard fragment instruction."});
                    return false;
                }
                const bool forbidden_discard = opcode == SpvOpKill || opcode == SpvOpTerminateInvocation ||
                                               opcode == SpvOpDemoteToHelperInvocation;
                const bool forbidden_output =
                    opcode == SpvOpDecorate && count >= 4u && words[offset + 2u] == SpvDecorationBuiltIn &&
                    (words[offset + 3u] == SpvBuiltInFragDepth || words[offset + 3u] == SpvBuiltInSampleMask ||
                     words[offset + 3u] == SpvBuiltInFragStencilRefEXT);
                if (forbidden_discard || forbidden_output)
                {
                    diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ReflectionMismatch, location,
                                           "Standard user functions cannot discard or write depth/sample coverage; use "
                                           "the shared CoverageFunction or Custom geometry."});
                    return false;
                }
                offset += count;
            }
            if (coverage)
            {
                for (const auto& binding : stage.reflection.bindings)
                {
                    if (binding.group != BindingGroup::Material && binding.group != BindingGroup::Object)
                    {
                        diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ReflectionMismatch, location,
                                               "CoverageFunction can use Material/Object resources only; View/Pass "
                                               "dependencies are unsupported."});
                        return false;
                    }
                }
            }
            return true;
        }

        void add_reflected_usage(std::vector<ParameterUsage>& usage, const ShaderStageReflection& reflection)
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

        bool validate_program_interfaces(const std::vector<ShaderCodeEntry>& stages, const SourceLocation& location,
                                         std::vector<Diagnostic>& diagnostics)
        {
            const auto vertex = std::find_if(stages.begin(), stages.end(),
                                             [](const auto& stage)
                                             {
                                                 return stage.reflection.stage == ShaderStageFlags::Vertex;
                                             });
            const auto pixel = std::find_if(stages.begin(), stages.end(),
                                            [](const auto& stage)
                                            {
                                                return stage.reflection.stage == ShaderStageFlags::Pixel;
                                            });
            if (vertex != stages.end() && pixel != stages.end())
            {
                for (const ReflectedInterfaceVariable& input : pixel->reflection.interface_variables)
                {
                    if (!input.input)
                    {
                        continue;
                    }
                    const auto output = std::find_if(
                        vertex->reflection.interface_variables.begin(), vertex->reflection.interface_variables.end(),
                        [&](const ReflectedInterfaceVariable& candidate)
                        {
                            return !candidate.input && candidate.location == input.location;
                        });
                    if (output == vertex->reflection.interface_variables.end() ||
                        (!input.semantic.empty() && !output->semantic.empty() && input.semantic != output->semantic) ||
                        (output != vertex->reflection.interface_variables.end() &&
                         (input.scalar_type != output->scalar_type ||
                          input.component_count != output->component_count)))
                    {
                        diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ReflectionMismatch, location,
                                               "Pixel input at location " + std::to_string(input.location) +
                                                   " does not match a vertex output."});
                    }
                }
            }
            const auto compute = std::find_if(stages.begin(), stages.end(),
                                              [](const auto& stage)
                                              {
                                                  return stage.reflection.stage == ShaderStageFlags::Compute;
                                              });
            if (compute != stages.end() &&
                (compute->reflection.thread_group_size_x == 0u || compute->reflection.thread_group_size_y == 0u ||
                 compute->reflection.thread_group_size_z == 0u))
            {
                diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ReflectionMismatch, location,
                                       "Compute Program reflection requires a non-zero thread-group size."});
            }
            return diagnostics.empty();
        }
    } // namespace

    bool supports_gpu_skin(const ShaderAsset& asset, const std::string& pass_name)
    {
        return supports_vertex_factory(asset.vertex_factory_support, VertexFactoryType::GPUSkin) &&
               std::any_of(asset.passes.begin(), asset.passes.end(),
                           [&](const ShaderPass& pass)
                           {
                               return pass.name == pass_name;
                           });
    }

    bool ShaderMapEntryCompileResult::succeeded() const
    {
        return entry.has_value() && diagnostics.empty();
    }

    ShaderMapEntryCompileResult compile_vulkan_shader_map_entry(const ShaderAsset& source_asset,
                                                                const ShaderProgramCompileInput& input,
                                                                const DiscoveredShaderToolchain& toolchain,
                                                                PlatformFile& platform_file,
                                                                const PhysicalPath& working_directory,
                                                                const ShaderProcessRunner& process_runner)
    {
        ShaderMapEntryCompileResult result;
        ShaderAsset expanded;
        if (!expand_standard_surface(source_asset, input.variant_selections, expanded, result.diagnostics))
        {
            return result;
        }
        const auto& asset = expanded;
        const auto pass = std::find_if(asset.passes.begin(), asset.passes.end(),
                                       [&](const ShaderPass& candidate)
                                       {
                                           return candidate.name == input.pass_name;
                                       });
        if (pass == asset.passes.end() || input.source_virtual_path.empty() || input.source_provider == nullptr ||
            pass->programs.empty() || asset.version != 2u)
        {
            result.diagnostics.push_back(
                {DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest, asset.location,
                 "Program compilation requires an existing Pass, entry points, and a virtual source path."});
            return result;
        }
        ShaderPermutationResult permutation = resolve_shader_permutation(asset, input.variant_selections);
        if (!permutation.succeeded())
        {
            result.diagnostics = std::move(permutation.diagnostics);
            return result;
        }

        ShaderProgramContract contract;
        contract.usage = asset.usage;
        contract.role = pass->role;
        contract.geometry = asset.geometry;
        contract.surface_mode =
            asset.geometry == ShaderGeometryMode::Standard
                ? (asset.passes.size() == 3u ? ShaderSurfaceMode::Masked : ShaderSurfaceMode::Opaque)
                : ShaderSurfaceMode::Explicit;
        contract.vertex_factory_support = asset.vertex_factory_support;
        contract.vertex_factory = input.vertex_factory;
        std::string contract_error;
        if (!validate_shader_program_contract(contract, contract_error))
        {
            result.diagnostics.push_back(
                {DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest, asset.location, contract_error});
            return result;
        }
        if (input.vertex_factory == VertexFactoryType::GPUSkin && !supports_gpu_skin(asset, input.pass_name))
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest,
                                          asset.location, "GPUSkin requires explicit VertexFactories support."});
            return result;
        }
        const auto plan = plan_shader_compilation(shader_compile_source(asset), {permutation.permutation->selections},
                                                  input.compile_policy);
        ShaderEngineFeatures features;
        std::string feature_error;
        if (!plan.succeeded() ||
            !resolve_shader_engine_features(shader_compile_source(asset), permutation.permutation->selections,
                                            input.compile_policy, features, feature_error))
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest,
                                          asset.location, plan.error.empty() ? feature_error : plan.error});
            return result;
        }
        const auto pass_configuration =
            resolve_shader_permutation(shader_pass_domain(pass->role, features), input.pass_selections);
        if (!pass_configuration.succeeded() ||
            std::none_of(plan.required.begin(), plan.required.end(),
                         [&](const auto& required)
                         {
                             return required.pass.name == pass->name &&
                                    required.vertex_factory == input.vertex_factory && pass_configuration.succeeded() &&
                                    required.pass_permutation.key == pass_configuration.permutation->key;
                         }))
        {
            result.diagnostics.push_back(
                {DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest, asset.location,
                 "Requested engine Pass configuration is invalid or filtered by compile policy."});
            return result;
        }
        permutation.permutation->generated_prelude += pass_configuration.permutation->generated_prelude;
        permutation.permutation->generated_prelude += input.vertex_factory == VertexFactoryType::GPUSkin
                                                          ? "#define TOY3D_GPU_SKIN 1\n"
                                                          : "#define TOY3D_GPU_SKIN 0\n";
        LogicalLayoutResult logical = compile_logical_layout(asset, input.vertex_factory, pass->role);
        if (!logical.succeeded())
        {
            result.diagnostics = std::move(logical.diagnostics);
            return result;
        }
        ShaderStageFlags program_stages = ShaderStageFlags::None;
        std::vector<EntryPoint> entry_points;
        for (const HlslBlock& block : pass->programs)
        {
            if (block.entry_points.size() != 1u)
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest,
                                              block.location, "Each stage block requires one entry point."});
                return result;
            }
            entry_points.push_back(block.entry_points.front());
            program_stages |= stage_flag(block.entry_points.front().stage);
        }
        ActiveLayoutResult discovery_active =
            build_active_layout(*logical.layout, all_parameter_usage(*logical.layout, program_stages));
        if (!discovery_active.succeeded())
        {
            result.diagnostics = std::move(discovery_active.diagnostics);
            return result;
        }
        TargetBindingResult discovery_mapping = allocate_target_bindings(
            *discovery_active.layout, ShaderTarget::VulkanSpirV, TargetBindingLimits::vulkan_portable_v1());
        if (!discovery_mapping.succeeded())
        {
            result.diagnostics = std::move(discovery_mapping.diagnostics);
            return result;
        }

        const std::string shader_include_source = combined_include_source(asset);
        const FileResult<PhysicalPath> discovery_root = platform_file.join_relative(working_directory, "discovery");
        const FileResult<PhysicalPath> final_root = platform_file.join_relative(working_directory, "final");
        if (!discovery_root.succeeded() || !final_root.succeeded())
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ShaderCompilationFailed,
                                          asset.location, "Unable to resolve Shader compiler working directories."});
            return result;
        }
        std::vector<ParameterUsage> reflected_usage;
        if (asset.geometry == ShaderGeometryMode::Standard && pass->role == ShaderPassRole::Forward)
        {
            for (const bool coverage : {false, true})
            {
                if (coverage && source_asset.passes.front().coverage_function.empty())
                {
                    continue;
                }
                const auto probe = standard_surface_probe(source_asset, coverage);
                const auto directory =
                    platform_file.join_relative(working_directory, coverage ? "coverage_probe" : "shading_probe");
                if (!directory.succeeded())
                {
                    result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest,
                                                  asset.location, directory.status().message});
                    return result;
                }
                auto compiled = compile_stage(input, *permutation.permutation, shader_material_domain(asset),
                                              shader_pass_domain(pass->role, features), asset.features, probe,
                                              probe.programs.back().entry_points.front(), shader_include_source,
                                              *logical.layout, *discovery_mapping.layout, toolchain, platform_file,
                                              directory.value(), false, process_runner);
                if (!compiled.stage)
                {
                    append_diagnostics(result.diagnostics, std::move(compiled.diagnostics));
                    return result;
                }
                if (!validate_standard_fragment(*compiled.stage, pass->location, coverage, result.diagnostics))
                {
                    return result;
                }
            }
        }
        for (const EntryPoint& entry : entry_points)
        {
            const FileResult<PhysicalPath> stage_working_directory =
                platform_file.join_relative(discovery_root.value(), stage_directory_name(stage_flag(entry.stage)));
            if (!stage_working_directory.succeeded())
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ShaderCompilationFailed,
                                              entry.location,
                                              "Unable to resolve discovery compile working directory."});
                return result;
            }
            StageCompileOutput discovered =
                compile_stage(input, *permutation.permutation, shader_material_domain(asset),
                              shader_pass_domain(pass->role, features), asset.features, *pass, entry,
                              shader_include_source, *logical.layout, *discovery_mapping.layout, toolchain,
                              platform_file, stage_working_directory.value(), false, process_runner);
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
        TargetBindingResult final_mapping = allocate_target_bindings(*final_active.layout, ShaderTarget::VulkanSpirV,
                                                                     TargetBindingLimits::vulkan_portable_v1());
        if (!final_mapping.succeeded())
        {
            result.diagnostics = std::move(final_mapping.diagnostics);
            return result;
        }

        ShaderMapEntry entry;
        entry.shader_name = asset.name;
        entry.pass_name = pass->name;
        entry.contract = contract;
        entry.target = ShaderTarget::VulkanSpirV;
        entry.profile = ShaderCompileProfile::VulkanES31;
        entry.logical_layout_hash = logical.layout->logical_layout_hash;
        entry.target_binding_hash = final_mapping.layout->target_binding_hash;
        entry.graphics_pass_state = pass->state;
        entry.pass_template_hash = calculate_shader_graphics_pass_state_hash(entry.graphics_pass_state);
        entry.variant_id_version = permutation.permutation->variant_id_version;
        entry.permutation_version = permutation.permutation->version;
        entry.permutation_key = permutation.permutation->key;
        entry.pass_permutation_key = pass_configuration.permutation->key;
        entry.mapping_version = final_mapping.layout->mapping_version;
        entry.parameter_schema = make_shader_parameter_schema(*logical.layout);
        result.editor_properties = logical.layout->editor_properties;
        for (const NativeBinding& binding : final_mapping.layout->bindings)
        {
            entry.bindings.push_back({binding.binding_id, binding.name, binding.group, binding.category, binding.stages,
                                      binding.register_class, binding.register_index, binding.descriptor_set,
                                      binding.descriptor_binding, binding.data_size, binding.data_layout_hash,
                                      binding.shader_abi_version});
        }
        for (const EntryPoint& entry_point : entry_points)
        {
            const FileResult<PhysicalPath> stage_working_directory =
                platform_file.join_relative(final_root.value(), stage_directory_name(stage_flag(entry_point.stage)));
            if (!stage_working_directory.succeeded())
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ShaderCompilationFailed,
                                              entry_point.location,
                                              "Unable to resolve final compile working directory."});
                return result;
            }
            StageCompileOutput compiled =
                compile_stage(input, *permutation.permutation, shader_material_domain(asset),
                              shader_pass_domain(pass->role, features), asset.features, *pass, entry_point,
                              shader_include_source, *logical.layout, *final_mapping.layout, toolchain, platform_file,
                              stage_working_directory.value(), true, process_runner);
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
} // namespace toy3d::shader
