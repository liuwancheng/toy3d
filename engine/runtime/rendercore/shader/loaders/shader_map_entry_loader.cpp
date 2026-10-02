#include "rendercore/shader/loaders/shader_map_entry_loader.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <utility>

#include "misc/sha256.h"
#include "rendercore/shader/shader_map_collection.h"
#include "shader/shader_map_entry.h"
#include "shader/shader_program_contract.h"

namespace toy3d
{
    // Conversion helpers use optional to reject unsupported reflection values
    // and withhold incomplete ShaderMap program data from runtime callers.
    namespace
    {
        RHIShaderStage to_rhi_stage(shader::ShaderStageFlags stage)
        {
            switch (stage)
            {
            case shader::ShaderStageFlags::Vertex:
                return RHIShaderStage::Vertex;
            case shader::ShaderStageFlags::Pixel:
                return RHIShaderStage::Pixel;
            case shader::ShaderStageFlags::Compute:
                return RHIShaderStage::Compute;
            default:
                return RHIShaderStage::Vertex;
            }
        }

        RHIShaderStageFlags to_rhi_stage_flags(shader::ShaderStageFlags stages)
        {
            RHIShaderStageFlags result = RHIShaderStageFlags::None;
            if (shader::has_stage(stages, shader::ShaderStageFlags::Vertex))
            {
                result |= RHIShaderStageFlags::Vertex;
            }
            if (shader::has_stage(stages, shader::ShaderStageFlags::Pixel))
            {
                result |= RHIShaderStageFlags::Pixel;
            }
            if (shader::has_stage(stages, shader::ShaderStageFlags::Compute))
            {
                result |= RHIShaderStageFlags::Compute;
            }
            return result;
        }

        RHIBindingGroup to_rhi_group(shader::BindingGroup group)
        {
            switch (group)
            {
            case shader::BindingGroup::Global:
                return RHIBindingGroup::Global;
            case shader::BindingGroup::View:
                return RHIBindingGroup::View;
            case shader::BindingGroup::Pass:
                return RHIBindingGroup::Pass;
            case shader::BindingGroup::Material:
                return RHIBindingGroup::Material;
            case shader::BindingGroup::Object:
                return RHIBindingGroup::Object;
            }
            return RHIBindingGroup::Max;
        }

        std::optional<RHIResourceBindingType> to_rhi_type(shader::ShaderParameterCategory category)
        {
            switch (category)
            {
            case shader::ShaderParameterCategory::Constant:
                return RHIResourceBindingType::UniformBuffer;
            case shader::ShaderParameterCategory::SampledTexture:
                return RHIResourceBindingType::SampledTexture;
            case shader::ShaderParameterCategory::Sampler:
                return RHIResourceBindingType::Sampler;
            case shader::ShaderParameterCategory::ReadOnlyBuffer:
                return RHIResourceBindingType::ReadOnlyBuffer;
            case shader::ShaderParameterCategory::StorageBuffer:
                return RHIResourceBindingType::StorageBuffer;
            case shader::ShaderParameterCategory::StorageTexture:
                return RHIResourceBindingType::StorageTexture;
            }
            return std::nullopt;
        }

        ShaderValueType to_shader_value_type(shader::ShaderValueType type)
        {
            switch (type)
            {
            case shader::ShaderValueType::Float32:
                return ShaderValueType::Float32;
            case shader::ShaderValueType::Float32x2:
                return ShaderValueType::Float32x2;
            case shader::ShaderValueType::Float32x3:
                return ShaderValueType::Float32x3;
            case shader::ShaderValueType::Float32x4:
                return ShaderValueType::Float32x4;
            case shader::ShaderValueType::Int32:
                return ShaderValueType::Int32;
            case shader::ShaderValueType::Int32x2:
                return ShaderValueType::Int32x2;
            case shader::ShaderValueType::Int32x3:
                return ShaderValueType::Int32x3;
            case shader::ShaderValueType::Int32x4:
                return ShaderValueType::Int32x4;
            case shader::ShaderValueType::UInt32:
                return ShaderValueType::UInt32;
            case shader::ShaderValueType::UInt32x2:
                return ShaderValueType::UInt32x2;
            case shader::ShaderValueType::UInt32x3:
                return ShaderValueType::UInt32x3;
            case shader::ShaderValueType::UInt32x4:
                return ShaderValueType::UInt32x4;
            case shader::ShaderValueType::Float32x2x2:
                return ShaderValueType::Float32x2x2;
            case shader::ShaderValueType::Float32x2x3:
                return ShaderValueType::Float32x2x3;
            case shader::ShaderValueType::Float32x2x4:
                return ShaderValueType::Float32x2x4;
            case shader::ShaderValueType::Float32x3x2:
                return ShaderValueType::Float32x3x2;
            case shader::ShaderValueType::Float32x3x3:
                return ShaderValueType::Float32x3x3;
            case shader::ShaderValueType::Float32x3x4:
                return ShaderValueType::Float32x3x4;
            case shader::ShaderValueType::Float32x4x2:
                return ShaderValueType::Float32x4x2;
            case shader::ShaderValueType::Float32x4x3:
                return ShaderValueType::Float32x4x3;
            case shader::ShaderValueType::Float32x4x4:
                return ShaderValueType::Float32x4x4;
            }
            return ShaderValueType::Float32;
        }

        bool same_reflected_layout(const shader::ReflectedBinding& left, const shader::ReflectedBinding& right)
        {
            if (left.array_count != right.array_count || left.constant_buffer_size != right.constant_buffer_size ||
                left.constant_members.size() != right.constant_members.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < left.constant_members.size(); ++index)
            {
                const shader::ReflectedConstantMember& a = left.constant_members[index];
                const shader::ReflectedConstantMember& b = right.constant_members[index];
                if (a.parameter_id != b.parameter_id || a.name != b.name || a.type != b.type || a.offset != b.offset ||
                    a.size != b.size || a.array_stride != b.array_stride || a.matrix_stride != b.matrix_stride)
                {
                    return false;
                }
            }
            return true;
        }

        ShaderMapBinding convert_binding(const shader::ShaderMapBinding& binding,
                                         const shader::ReflectedBinding& reflected)
        {
            ShaderMapBinding result;
            result.parameter_id = binding.binding_id;
            result.name = binding.name;
            result.group = to_rhi_group(binding.group);
            result.type = *to_rhi_type(binding.category);
            // C++17 optional preserves whether reflection identified a resource kind.
            if (binding.category == shader::ShaderParameterCategory::ReadOnlyBuffer &&
                reflected.resource_kind == shader::ResourceKind::Buffer)
            {
                result.type = RHIResourceBindingType::ReadOnlyTypedBuffer;
            }
            result.stages = to_rhi_stage_flags(binding.stages);
            result.target_binding = binding.descriptor_binding;
            result.array_count = reflected.array_count;
            result.constant_buffer_size = reflected.constant_buffer_size;
            result.data_layout_hash = reflected.data_layout_hash;
            result.shader_abi_version = reflected.shader_abi_version;
            for (const shader::ReflectedConstantMember& member : reflected.constant_members)
            {
                result.constant_members.push_back({member.parameter_id, member.name, to_shader_value_type(member.type),
                                                   member.offset, member.size, member.array_stride,
                                                   member.matrix_stride});
            }
            return result;
        }

        std::optional<ShaderMapProgramData> convert_entry(const shader::ShaderMapEntry& entry, std::string& error)
        {
            if (entry.target != shader::ShaderTarget::VulkanSpirV ||
                entry.profile != shader::ShaderCompileProfile::VulkanES31)
            {
                error = "ShaderMapEntry loader only supports Vulkan ES3.1 profile entries.";
                return std::nullopt;
            }
            ShaderMapProgramData program;
            program.shader_name = entry.shader_name;
            program.pass_name = entry.pass_name;
            program.contract = entry.contract;
            program.platform = ShaderPlatform::VulkanES31;
            program.mapping_version = entry.mapping_version;
            program.logical_layout_hash = entry.logical_layout_hash;
            program.target_binding_hash = entry.target_binding_hash;
            program.graphics_pass_state = entry.graphics_pass_state;
            program.pass_template_hash = entry.pass_template_hash;
            program.permutation_key = entry.permutation_key;
            program.parameter_schema = entry.parameter_schema;

            for (const shader::ShaderMapBinding& binding : entry.bindings)
            {
                const auto type = to_rhi_type(binding.category);
                if (!type)
                {
                    error = "ShaderMap entry contains an unsupported binding category.";
                    return std::nullopt;
                }
                if (to_rhi_group(binding.group) == RHIBindingGroup::Max)
                {
                    error = "ShaderMap entry contains an unsupported binding group.";
                    return std::nullopt;
                }
                const shader::ReflectedBinding* reflected_layout = nullptr;
                for (const shader::ShaderCodeEntry& stage : entry.stages)
                {
                    const auto reflected =
                        std::find_if(stage.reflection.bindings.begin(), stage.reflection.bindings.end(),
                                     [&](const shader::ReflectedBinding& value)
                                     {
                                         return value.parameter_id == binding.binding_id;
                                     });
                    if (reflected == stage.reflection.bindings.end())
                    {
                        continue;
                    }
                    if (reflected_layout && !same_reflected_layout(*reflected_layout, *reflected))
                    {
                        error = "ShaderMap entry has inconsistent reflected binding metadata.";
                        return std::nullopt;
                    }
                    reflected_layout = &*reflected;
                }
                if (!reflected_layout)
                {
                    error = "ShaderMap entry binding is absent from stage reflection.";
                    return std::nullopt;
                }
                program.bindings.push_back(convert_binding(binding, *reflected_layout));
            }

            for (const shader::ShaderCodeEntry& stage : entry.stages)
            {
                ShaderMapStage output;
                output.stage = to_rhi_stage(stage.request.stage);
                output.entry_point = stage.request.entry_point;
                output.binary = stage.binary;
                output.content_hash = sha256(stage.binary);
                for (const shader::ReflectedBinding& reflected : stage.reflection.bindings)
                {
                    const auto mapping = std::find_if(entry.bindings.begin(), entry.bindings.end(),
                                                      [&](const shader::ShaderMapBinding& binding)
                                                      {
                                                          return binding.binding_id == reflected.parameter_id;
                                                      });
                    if (mapping == entry.bindings.end() || !to_rhi_type(mapping->category))
                    {
                        error = "ShaderMap reflection has no supported target mapping.";
                        return std::nullopt;
                    }
                    output.reflection.push_back(convert_binding(*mapping, reflected));
                }
                output.interface_variables = stage.reflection.interface_variables;
                if (output.stage == RHIShaderStage::Vertex)
                {
                    std::set<ShaderVertexAttributeId> attributes;
                    for (const shader::ReflectedInterfaceVariable& reflected : output.interface_variables)
                    {
                        if (!reflected.input)
                        {
                            continue;
                        }
                        ShaderVertexInput vertex_input;
                        if (!try_make_shader_vertex_input(reflected, vertex_input, error))
                        {
                            return std::nullopt;
                        }
                        if (!attributes.insert(vertex_input.attribute_id).second)
                        {
                            error = "ShaderMap vertex inputs contain a duplicate logical attribute.";
                            return std::nullopt;
                        }
                        program.vertex_inputs.push_back(std::move(vertex_input));
                    }
                }
                program.stages.push_back(std::move(output));
            }
            return program;
        }

        std::string diagnostics_text(const std::vector<std::string>& diagnostics)
        {
            std::ostringstream stream;
            for (std::size_t index = 0; index < diagnostics.size(); ++index)
            {
                if (index != 0)
                {
                    stream << ' ';
                }
                stream << diagnostics[index];
            }
            return stream.str();
        }
    } // namespace

    ShaderMapEntryLoader::ShaderMapEntryLoader(PhysicalPath entry_root) : entry_root_(std::move(entry_root))
    {
    }

    ShaderMapProgramLoadResult ShaderMapEntryLoader::load_program(const ShaderMapProgramKey& key) const
    {
        ShaderMapProgramLoadResult result;
        if (key.platform != ShaderPlatform::VulkanES31)
        {
            result.error = "ShaderMapEntry loader does not support the requested ShaderPlatform.";
            return result;
        }
        auto candidate =
            ShaderMapCollection::create_candidate(load_collection(key.shader_name, key.platform, key.permutation_key));
        if (!candidate.succeeded())
        {
            result.error = std::move(candidate.error);
            return result;
        }
        auto selected = candidate.collection->find(key.role, key.vertex_factory,
                                                   key.role == shader::ShaderPassRole::Global ? key.pass_name : "");
        if (!selected.succeeded())
        {
            result.error = std::move(selected.error);
            return result;
        }
        if (selected.program->data().pass_name != key.pass_name)
        {
            result.error = "ShaderMap query Pass name does not match its declared role.";
            return result;
        }
        return validate_shader_map_program(selected.program->data(), key);
    }

    ShaderMapCollectionLoadResult ShaderMapEntryLoader::load_collection(const std::string& name,
                                                                        ShaderPlatform platform,
                                                                        const ShaderContentHash& permutation) const
    {
        ShaderMapCollectionLoadResult result;
        if (platform != ShaderPlatform::VulkanES31)
        {
            result.error = "ShaderMapEntry collection loader does not support the requested ShaderPlatform.";
            return result;
        }
        if (!shader::read_shader_map_index(platform_file_, entry_root_, name, shader::ShaderTarget::VulkanSpirV,
                                           shader::ShaderCompileProfile::VulkanES31, permutation, result.index,
                                           result.error))
        {
            return result;
        }
        for (const auto& record : result.index.programs)
        {
            const auto read = shader::read_verified_shader_map_entry(platform_file_, entry_root_, record.entry_key);
            if (!shader::shader_map_index_matches_entry(result.index, record, read))
            {
                result.programs.clear();
                result.error = "ShaderMap collection contains a missing, damaged or mismatched entry: " +
                               diagnostics_text(read.diagnostics);
                return result;
            }
            std::string error;
            auto program = convert_entry(*read.entry, error);
            if (!program)
            {
                result.programs.clear();
                result.error = std::move(error);
                return result;
            }
            ShaderMapProgramKey key;
            key.shader_name = name;
            key.pass_name = record.pass_name;
            key.platform = platform;
            key.permutation_key = permutation;
            key.role = record.contract.role;
            key.vertex_factory = record.contract.vertex_factory;
            auto validated = validate_shader_map_program(std::move(*program), key);
            if (!validated.succeeded())
            {
                result.programs.clear();
                result.error = std::move(validated.error);
                return result;
            }
            result.programs.push_back(std::move(*validated.program));
        }
        return result;
    }
} // namespace toy3d
