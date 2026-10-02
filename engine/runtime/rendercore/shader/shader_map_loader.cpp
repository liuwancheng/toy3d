#include "rendercore/shader/shader_map_loader.h"

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <utility>

#include "shader/shader_program_contract.h"

namespace toy3d
{
    namespace
    {
        bool hash_is_zero(const ShaderContentHash& hash)
        {
            return std::all_of(hash.begin(), hash.end(),
                               [](std::uint8_t byte)
                               {
                                   return byte == 0u;
                               });
        }

        bool key_matches(const ShaderMapProgramData& program, const ShaderMapProgramKey& key)
        {
            return program.shader_name == key.shader_name && program.pass_name == key.pass_name &&
                   program.platform == key.platform && program.permutation_key == key.permutation_key &&
                   program.contract.role == key.role && program.contract.vertex_factory == key.vertex_factory;
        }

        shader::BindingGroup to_shader_group(RHIBindingGroup group)
        {
            switch (group)
            {
            case RHIBindingGroup::Global:
                return shader::BindingGroup::Global;
            case RHIBindingGroup::View:
                return shader::BindingGroup::View;
            case RHIBindingGroup::Pass:
                return shader::BindingGroup::Pass;
            case RHIBindingGroup::Material:
                return shader::BindingGroup::Material;
            case RHIBindingGroup::Object:
                return shader::BindingGroup::Object;
            case RHIBindingGroup::Max:
                break;
            }
            return shader::BindingGroup::Material;
        }

        shader::ShaderParameterCategory to_shader_category(RHIResourceBindingType type)
        {
            switch (type)
            {
            case RHIResourceBindingType::UniformBuffer:
                return shader::ShaderParameterCategory::Constant;
            case RHIResourceBindingType::SampledTexture:
                return shader::ShaderParameterCategory::SampledTexture;
            case RHIResourceBindingType::Sampler:
                return shader::ShaderParameterCategory::Sampler;
            case RHIResourceBindingType::ReadOnlyBuffer:
            case RHIResourceBindingType::ReadOnlyTypedBuffer:
                return shader::ShaderParameterCategory::ReadOnlyBuffer;
            case RHIResourceBindingType::StorageBuffer:
                return shader::ShaderParameterCategory::StorageBuffer;
            }
            return shader::ShaderParameterCategory::Constant;
        }

        bool valid_binding_type(RHIResourceBindingType type)
        {
            switch (type)
            {
            case RHIResourceBindingType::UniformBuffer:
            case RHIResourceBindingType::SampledTexture:
            case RHIResourceBindingType::Sampler:
            case RHIResourceBindingType::ReadOnlyBuffer:
            case RHIResourceBindingType::ReadOnlyTypedBuffer:
            case RHIResourceBindingType::StorageBuffer:
                return true;
            }
            return false;
        }

        bool same_value_type(shader::ShaderValueType expected, ShaderValueType actual)
        {
            switch (expected)
            {
            case shader::ShaderValueType::Float32:
                return actual == ShaderValueType::Float32;
            case shader::ShaderValueType::Float32x2:
                return actual == ShaderValueType::Float32x2;
            case shader::ShaderValueType::Float32x3:
                return actual == ShaderValueType::Float32x3;
            case shader::ShaderValueType::Float32x4:
                return actual == ShaderValueType::Float32x4;
            case shader::ShaderValueType::Int32:
                return actual == ShaderValueType::Int32;
            case shader::ShaderValueType::Int32x2:
                return actual == ShaderValueType::Int32x2;
            case shader::ShaderValueType::Int32x3:
                return actual == ShaderValueType::Int32x3;
            case shader::ShaderValueType::Int32x4:
                return actual == ShaderValueType::Int32x4;
            case shader::ShaderValueType::UInt32:
                return actual == ShaderValueType::UInt32;
            case shader::ShaderValueType::UInt32x2:
                return actual == ShaderValueType::UInt32x2;
            case shader::ShaderValueType::UInt32x3:
                return actual == ShaderValueType::UInt32x3;
            case shader::ShaderValueType::UInt32x4:
                return actual == ShaderValueType::UInt32x4;
            case shader::ShaderValueType::Float32x2x2:
                return actual == ShaderValueType::Float32x2x2;
            case shader::ShaderValueType::Float32x2x3:
                return actual == ShaderValueType::Float32x2x3;
            case shader::ShaderValueType::Float32x2x4:
                return actual == ShaderValueType::Float32x2x4;
            case shader::ShaderValueType::Float32x3x2:
                return actual == ShaderValueType::Float32x3x2;
            case shader::ShaderValueType::Float32x3x3:
                return actual == ShaderValueType::Float32x3x3;
            case shader::ShaderValueType::Float32x3x4:
                return actual == ShaderValueType::Float32x3x4;
            case shader::ShaderValueType::Float32x4x2:
                return actual == ShaderValueType::Float32x4x2;
            case shader::ShaderValueType::Float32x4x3:
                return actual == ShaderValueType::Float32x4x3;
            case shader::ShaderValueType::Float32x4x4:
                return actual == ShaderValueType::Float32x4x4;
            }
            return false;
        }

        bool validate_program_schema_subset(const ShaderMapProgramData& program, std::string& error)
        {
            if (!shader::validate_shader_parameter_schema(program.parameter_schema, error) ||
                program.logical_layout_hash != program.parameter_schema.logical_layout_hash)
            {
                return false;
            }
            for (const ShaderMapBinding& binding : program.bindings)
            {
                if (binding.type == RHIResourceBindingType::UniformBuffer)
                {
                    const auto buffer = std::find_if(program.parameter_schema.constant_buffers.begin(),
                                                     program.parameter_schema.constant_buffers.end(),
                                                     [&](const shader::ShaderParameterConstantBufferSchema& candidate)
                                                     {
                                                         return candidate.binding_id == binding.parameter_id;
                                                     });
                    if (buffer == program.parameter_schema.constant_buffers.end() || buffer->name != binding.name ||
                        buffer->group != to_shader_group(binding.group) ||
                        buffer->size != binding.constant_buffer_size ||
                        buffer->data_layout_hash != binding.data_layout_hash ||
                        buffer->shader_abi_version != binding.shader_abi_version ||
                        buffer->members.size() != binding.constant_members.size())
                    {
                        error = "ShaderMap Program active constant binding is not part of its complete schema.";
                        return false;
                    }
                    for (std::size_t index = 0; index < buffer->members.size(); ++index)
                    {
                        const shader::ShaderParameterConstantMemberSchema& expected = buffer->members[index];
                        const ShaderMapBinding::ConstantMember& actual = binding.constant_members[index];
                        if (expected.parameter_id != actual.parameter_id || expected.name != actual.name ||
                            !same_value_type(expected.type, actual.type) || expected.offset != actual.offset ||
                            expected.size != actual.size || expected.array_stride != actual.array_stride ||
                            expected.matrix_stride != actual.matrix_stride)
                        {
                            error = "ShaderMap Program active constant layout differs from its complete schema.";
                            return false;
                        }
                    }
                }
                else
                {
                    const auto resource = std::find_if(program.parameter_schema.resources.begin(),
                                                       program.parameter_schema.resources.end(),
                                                       [&](const shader::ShaderParameterResourceSchema& candidate)
                                                       {
                                                           return candidate.parameter_id == binding.parameter_id;
                                                       });
                    if (resource == program.parameter_schema.resources.end() || resource->name != binding.name ||
                        resource->group != to_shader_group(binding.group) ||
                        resource->category != to_shader_category(binding.type) ||
                        resource->array_count != binding.array_count)
                    {
                        error = "ShaderMap Program active resource binding is not part of its complete schema.";
                        return false;
                    }
                    if (resource->category == shader::ShaderParameterCategory::ReadOnlyBuffer &&
                        (resource->resource_kind == shader::ResourceKind::Buffer) !=
                            (binding.type == RHIResourceBindingType::ReadOnlyTypedBuffer))
                    {
                        error = "ShaderMap Program buffer binding kind differs from its complete schema.";
                        return false;
                    }
                }
            }
            return true;
        }

        std::uint32_t vulkan_portable_set(RHIBindingGroup group)
        {
            switch (group)
            {
            case RHIBindingGroup::Global:
            case RHIBindingGroup::View:
                return 0;
            case RHIBindingGroup::Pass:
                return 1;
            case RHIBindingGroup::Material:
                return 2;
            case RHIBindingGroup::Object:
                return 3;
            case RHIBindingGroup::Max:
                break;
            }
            return 4;
        }

        std::string normalize_interface_semantic(std::string semantic)
        {
            for (char& character : semantic)
            {
                if (character >= 'a' && character <= 'z')
                {
                    character = static_cast<char>(character - ('a' - 'A'));
                }
            }
            return semantic;
        }

        bool validate_stage_interfaces(const ShaderMapStage& stage, std::string& error)
        {
            std::set<std::pair<bool, std::uint32_t>> stage_locations;
            std::set<ShaderVertexAttributeId> vertex_logical_attributes;
            for (const shader::ReflectedInterfaceVariable& variable : stage.interface_variables)
            {
                if (variable.location == std::numeric_limits<std::uint32_t>::max() ||
                    static_cast<std::uint32_t>(variable.scalar_type) >
                        static_cast<std::uint32_t>(shader::ReflectedInterfaceVariable::ScalarType::UInt32) ||
                    variable.component_count == 0u || variable.component_count > 4u ||
                    !stage_locations.emplace(variable.input, variable.location).second)
                {
                    error = "ShaderMap stage interface has an invalid or duplicate target mapping.";
                    return false;
                }

                if (stage.stage != RHIShaderStage::Vertex || !variable.input)
                {
                    continue;
                }

                ShaderVertexInput vertex_input;
                if (!try_make_shader_vertex_input(variable, vertex_input, error))
                {
                    return false;
                }
                if (!vertex_logical_attributes.insert(vertex_input.attribute_id).second)
                {
                    error = "ShaderMap vertex inputs contain a duplicate logical attribute.";
                    return false;
                }
            }
            return true;
        }

        bool validate_graphics_stage_interfaces(const ShaderMapStage& vertex_stage, const ShaderMapStage& pixel_stage,
                                                std::string& error)
        {
            for (const shader::ReflectedInterfaceVariable& input : pixel_stage.interface_variables)
            {
                if (!input.input)
                {
                    continue;
                }
                const auto output =
                    std::find_if(vertex_stage.interface_variables.begin(), vertex_stage.interface_variables.end(),
                                 [&](const shader::ReflectedInterfaceVariable& candidate)
                                 {
                                     return !candidate.input && candidate.location == input.location;
                                 });
                if (output == vertex_stage.interface_variables.end() || input.scalar_type != output->scalar_type ||
                    input.component_count != output->component_count ||
                    (!input.semantic.empty() && !output->semantic.empty() &&
                     normalize_interface_semantic(input.semantic) != normalize_interface_semantic(output->semantic)))
                {
                    error = "ShaderMap pixel input conflicts with the vertex-stage Program output.";
                    return false;
                }
            }
            return true;
        }

        bool validate_program_vertex_inputs(const ShaderMapProgramData& program, const ShaderMapStage* vertex_stage,
                                            std::string& error)
        {
            std::vector<ShaderVertexInput> expected_inputs;
            std::set<ShaderVertexAttributeId> expected_attributes;
            if (vertex_stage != nullptr)
            {
                for (const shader::ReflectedInterfaceVariable& reflected : vertex_stage->interface_variables)
                {
                    if (!reflected.input)
                    {
                        continue;
                    }
                    ShaderVertexInput expected;
                    if (!try_make_shader_vertex_input(reflected, expected, error) ||
                        !expected_attributes.insert(expected.attribute_id).second)
                    {
                        if (error.empty())
                        {
                            error = "ShaderMap vertex inputs contain a duplicate logical attribute.";
                        }
                        return false;
                    }
                    expected_inputs.push_back(std::move(expected));
                }
            }

            if (program.vertex_inputs.size() != expected_inputs.size())
            {
                error = "ShaderMap Program vertex inputs do not match stage reflection.";
                return false;
            }
            for (const ShaderVertexInput& expected : expected_inputs)
            {
                const auto actual = std::find_if(program.vertex_inputs.begin(), program.vertex_inputs.end(),
                                                 [&](const ShaderVertexInput& candidate)
                                                 {
                                                     return candidate.attribute_id == expected.attribute_id;
                                                 });
                if (actual == program.vertex_inputs.end() ||
                    !have_same_shader_vertex_input_contract(*actual, expected) ||
                    actual->target_location != expected.target_location)
                {
                    error = "ShaderMap Program vertex input conflicts with stage reflection.";
                    return false;
                }
            }
            return true;
        }
    } // namespace

    bool ShaderMapProgramLoadResult::succeeded() const
    {
        return program.has_value() && error.empty();
    }

    bool ShaderMapCollectionLoadResult::succeeded() const
    {
        return !programs.empty() && error.empty();
    }

    ShaderMapCollectionLoadResult ShaderMapLoader::load_collection(const std::string&, ShaderPlatform,
                                                                   const ShaderContentHash&) const
    {
        ShaderMapCollectionLoadResult result;
        result.error = "This ShaderMap loader does not support complete program collections.";
        return result;
    }

    ShaderMapProgramLoadResult validate_shader_map_program(ShaderMapProgramData program, const ShaderMapProgramKey& key)
    {
        ShaderMapProgramLoadResult result;
        if (!shader::validate_shader_program_contract(program.contract, result.error) ||
            !validate_program_schema_subset(program, result.error))
        {
            return result;
        }
        if (key.shader_name.empty() || key.pass_name.empty() || hash_is_zero(key.permutation_key))
        {
            result.error = "ShaderMap key requires shader/pass names and a permutation key.";
            return result;
        }
        if (!key_matches(program, key))
        {
            result.error = "ShaderMap program identity, platform, or permutation does not match the key.";
            return result;
        }
        if (program.mapping_version == 0 || hash_is_zero(program.logical_layout_hash) ||
            hash_is_zero(program.target_binding_hash) || hash_is_zero(program.pass_template_hash) ||
            !shader::is_valid_shader_graphics_pass_state(program.graphics_pass_state) ||
            shader::calculate_shader_graphics_pass_state_hash(program.graphics_pass_state) !=
                program.pass_template_hash ||
            hash_is_zero(program.permutation_key))
        {
            result.error = "ShaderMap program contains an invalid version or stable hash.";
            return result;
        }

        std::set<std::tuple<RHIBindingGroup, RHIResourceBindingType, std::uint32_t>> binding_keys;
        std::set<std::pair<std::uint32_t, std::uint32_t>> vulkan_bindings;
        std::set<ShaderParameterId> parameter_ids;
        for (const ShaderMapBinding& binding : program.bindings)
        {
            if (binding.parameter_id == 0 || binding.name.empty() || binding.group >= RHIBindingGroup::Max ||
                !valid_binding_type(binding.type) || binding.stages == RHIShaderStageFlags::None ||
                binding.array_count == 0 || !parameter_ids.insert(binding.parameter_id).second ||
                !binding_keys.emplace(binding.group, binding.type, binding.target_binding).second)
            {
                result.error = "ShaderMap program contains an invalid or duplicate binding.";
                return result;
            }
            if (binding.type == RHIResourceBindingType::UniformBuffer)
            {
                if (binding.constant_buffer_size == 0 || binding.constant_members.empty())
                {
                    result.error = "ShaderMap constant-buffer binding has no runtime layout metadata.";
                    return result;
                }
                std::set<std::string> member_names;
                for (const ShaderMapBinding::ConstantMember& member : binding.constant_members)
                {
                    if (member.parameter_id == 0 || member.name.empty() || member.size == 0 ||
                        static_cast<std::uint32_t>(member.type) >
                            static_cast<std::uint32_t>(ShaderValueType::Float32x4x4) ||
                        member.offset > binding.constant_buffer_size ||
                        member.size > binding.constant_buffer_size - member.offset ||
                        !parameter_ids.insert(member.parameter_id).second || !member_names.insert(member.name).second)
                    {
                        result.error = "ShaderMap constant-buffer member metadata is invalid or duplicate.";
                        return result;
                    }
                }
            }
            else if (binding.constant_buffer_size != 0 || !binding.constant_members.empty())
            {
                result.error = "ShaderMap resource binding contains constant-buffer metadata.";
                return result;
            }
            if (program.platform == ShaderPlatform::VulkanES31)
            {
                const std::uint32_t set = vulkan_portable_set(binding.group);
                if (set >= 4 || !vulkan_bindings.emplace(set, binding.target_binding).second)
                {
                    result.error = "ShaderMap program contains a duplicate Vulkan set/binding.";
                    return result;
                }
            }
        }

        RHIShaderStageFlags stage_mask = RHIShaderStageFlags::None;
        const ShaderMapStage* vertex_stage = nullptr;
        const ShaderMapStage* pixel_stage = nullptr;
        for (const ShaderMapStage& stage : program.stages)
        {
            RHIShaderStageFlags stage_flag = RHIShaderStageFlags::None;
            switch (stage.stage)
            {
            case RHIShaderStage::Vertex:
                stage_flag = RHIShaderStageFlags::Vertex;
                vertex_stage = &stage;
                break;
            case RHIShaderStage::Pixel:
                stage_flag = RHIShaderStageFlags::Pixel;
                pixel_stage = &stage;
                break;
            case RHIShaderStage::Compute:
                stage_flag = RHIShaderStageFlags::Compute;
                break;
            default:
                result.error = "ShaderMap program contains an unsupported shader stage.";
                return result;
            }
            if (EnumHasAnyFlags(stage_mask, stage_flag) || stage.entry_point.empty() || stage.binary.empty() ||
                hash_is_zero(stage.content_hash))
            {
                result.error = "ShaderMap program contains an invalid or duplicate stage.";
                return result;
            }
            stage_mask |= stage_flag;
            if (!validate_stage_interfaces(stage, result.error))
            {
                return result;
            }
            for (const ShaderMapBinding& reflected : stage.reflection)
            {
                const auto expected = std::find_if(program.bindings.begin(), program.bindings.end(),
                                                   [&](const ShaderMapBinding& binding)
                                                   {
                                                       return binding.parameter_id == reflected.parameter_id;
                                                   });
                if (expected == program.bindings.end() || expected->name != reflected.name ||
                    expected->group != reflected.group || expected->type != reflected.type ||
                    expected->target_binding != reflected.target_binding ||
                    expected->array_count != reflected.array_count ||
                    expected->constant_buffer_size != reflected.constant_buffer_size ||
                    expected->constant_members.size() != reflected.constant_members.size() ||
                    !EnumHasAnyFlags(expected->stages, stage_flag))
                {
                    result.error = "ShaderMap stage reflection does not match the Program binding layout.";
                    return result;
                }
                for (std::size_t index = 0; index < expected->constant_members.size(); ++index)
                {
                    const ShaderMapBinding::ConstantMember& expected_member = expected->constant_members[index];
                    const ShaderMapBinding::ConstantMember& reflected_member = reflected.constant_members[index];
                    if (expected_member.parameter_id != reflected_member.parameter_id ||
                        expected_member.name != reflected_member.name ||
                        expected_member.type != reflected_member.type ||
                        expected_member.offset != reflected_member.offset ||
                        expected_member.size != reflected_member.size ||
                        expected_member.array_stride != reflected_member.array_stride ||
                        expected_member.matrix_stride != reflected_member.matrix_stride)
                    {
                        result.error = "ShaderMap stage constant layout does not match the Program binding layout.";
                        return result;
                    }
                }
            }
        }

        const bool graphics = stage_mask == RHIShaderStageFlags::Vertex ||
                              stage_mask == (RHIShaderStageFlags::Vertex | RHIShaderStageFlags::Pixel);
        const bool compute = stage_mask == RHIShaderStageFlags::Compute;
        if ((!graphics && !compute) || program.stages.empty())
        {
            result.error = "ShaderMap program has an invalid graphics/compute stage set.";
            return result;
        }
        shader::ShaderStageFlags contract_stages = shader::ShaderStageFlags::Compute;
        if (graphics)
        {
            contract_stages = shader::ShaderStageFlags::Vertex;
            if (pixel_stage != nullptr)
            {
                contract_stages = contract_stages | shader::ShaderStageFlags::Pixel;
            }
        }
        if (!shader::validate_shader_program_stages(program.contract, contract_stages, result.error))
        {
            return result;
        }
        if (vertex_stage != nullptr && pixel_stage != nullptr &&
            !validate_graphics_stage_interfaces(*vertex_stage, *pixel_stage, result.error))
        {
            return result;
        }
        if (!validate_program_vertex_inputs(program, vertex_stage, result.error))
        {
            return result;
        }
        for (const ShaderMapBinding& binding : program.bindings)
        {
            const std::uint32_t binding_stages = static_cast<std::uint32_t>(binding.stages);
            const std::uint32_t present_stages = static_cast<std::uint32_t>(stage_mask);
            if ((binding_stages & ~present_stages) != 0u)
            {
                result.error = "ShaderMap binding visibility references an absent stage.";
                return result;
            }
            for (const ShaderMapStage& stage : program.stages)
            {
                RHIShaderStageFlags stage_flag = RHIShaderStageFlags::None;
                if (stage.stage == RHIShaderStage::Vertex)
                {
                    stage_flag = RHIShaderStageFlags::Vertex;
                }
                else if (stage.stage == RHIShaderStage::Pixel)
                {
                    stage_flag = RHIShaderStageFlags::Pixel;
                }
                else if (stage.stage == RHIShaderStage::Compute)
                {
                    stage_flag = RHIShaderStageFlags::Compute;
                }
                if (!EnumHasAnyFlags(binding.stages, stage_flag))
                {
                    continue;
                }
                const bool reflected = std::any_of(stage.reflection.begin(), stage.reflection.end(),
                                                   [&](const ShaderMapBinding& value)
                                                   {
                                                       return value.parameter_id == binding.parameter_id;
                                                   });
                if (!reflected)
                {
                    result.error = "ShaderMap binding is missing from a required stage reflection.";
                    return result;
                }
            }
        }
        result.program = std::move(program);
        return result;
    }
} // namespace toy3d
