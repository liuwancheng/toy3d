#include "rendercore/shader/shader_map_loader.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool hash_is_zero(const ShaderContentHash& hash)
        {
            return std::all_of(hash.begin(), hash.end(),
                [](std::uint8_t byte) { return byte == 0u; });
        }

        bool key_matches(
            const ShaderMapProgramData& program,
            const ShaderMapProgramKey& key)
        {
            return program.shader_name == key.shader_name &&
                program.pass_name == key.pass_name &&
                program.platform == key.platform &&
                program.permutation_key == key.permutation_key;
        }

        std::uint32_t vulkan_portable_set(RHIBindingGroup group)
        {
            switch (group)
            {
            case RHIBindingGroup::Global:
            case RHIBindingGroup::View: return 0;
            case RHIBindingGroup::Pass: return 1;
            case RHIBindingGroup::Material: return 2;
            case RHIBindingGroup::Object: return 3;
            case RHIBindingGroup::Max: break;
            }
            return 4;
        }
    }

    bool ShaderMapProgramLoadResult::succeeded() const
    {
        return program.has_value() && error.empty();
    }

    ShaderMapProgramLoadResult validate_shader_map_program(
        ShaderMapProgramData program,
        const ShaderMapProgramKey& key)
    {
        ShaderMapProgramLoadResult result;
        if (key.shader_name.empty() || key.pass_name.empty() ||
            hash_is_zero(key.permutation_key))
        {
            result.error = "ShaderMap key requires shader/pass names and a permutation key.";
            return result;
        }
        if (!key_matches(program, key))
        {
            result.error = "ShaderMap program identity, platform, or permutation does not match the key.";
            return result;
        }
        if (program.mapping_version == 0 ||
            hash_is_zero(program.logical_layout_hash) ||
            hash_is_zero(program.target_binding_hash) ||
            hash_is_zero(program.pass_template_hash) ||
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
            if (binding.parameter_id == 0 || binding.name.empty() ||
                binding.group >= RHIBindingGroup::Max ||
                static_cast<std::uint32_t>(binding.type) >
                    static_cast<std::uint32_t>(RHIResourceBindingType::StorageBuffer) ||
                binding.stages == RHIShaderStageFlags::None || binding.array_count == 0 ||
                !parameter_ids.insert(binding.parameter_id).second ||
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
                        !parameter_ids.insert(member.parameter_id).second ||
                        !member_names.insert(member.name).second)
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
            if (program.platform == ShaderPlatform::VulkanPortableV1)
            {
                const std::uint32_t set = vulkan_portable_set(binding.group);
                if (set >= 4 ||
                    !vulkan_bindings.emplace(set, binding.target_binding).second)
                {
                    result.error = "ShaderMap program contains a duplicate Vulkan set/binding.";
                    return result;
                }
            }
        }

        RHIShaderStageFlags stage_mask = RHIShaderStageFlags::None;
        for (const ShaderMapStage& stage : program.stages)
        {
            RHIShaderStageFlags stage_flag = RHIShaderStageFlags::None;
            switch (stage.stage)
            {
            case RHIShaderStage::Vertex: stage_flag = RHIShaderStageFlags::Vertex; break;
            case RHIShaderStage::Pixel: stage_flag = RHIShaderStageFlags::Pixel; break;
            case RHIShaderStage::Compute: stage_flag = RHIShaderStageFlags::Compute; break;
            default:
                result.error = "ShaderMap program contains an unsupported shader stage.";
                return result;
            }
            if (rhi_has_any_flag(stage_mask, stage_flag) || stage.entry_point.empty() ||
                stage.binary.empty() || hash_is_zero(stage.content_hash))
            {
                result.error = "ShaderMap program contains an invalid or duplicate stage.";
                return result;
            }
            stage_mask = rhi_enum_or(stage_mask, stage_flag);
            for (const ShaderMapBinding& reflected : stage.reflection)
            {
                const auto expected = std::find_if(program.bindings.begin(), program.bindings.end(),
                    [&](const ShaderMapBinding& binding) {
                        return binding.parameter_id == reflected.parameter_id;
                    });
                if (expected == program.bindings.end() || expected->name != reflected.name ||
                    expected->group != reflected.group || expected->type != reflected.type ||
                    expected->target_binding != reflected.target_binding ||
                    expected->array_count != reflected.array_count ||
                    expected->constant_buffer_size != reflected.constant_buffer_size ||
                    expected->constant_members.size() != reflected.constant_members.size() ||
                    !rhi_has_any_flag(expected->stages, stage_flag))
                {
                    result.error = "ShaderMap stage reflection does not match the Program binding layout.";
                    return result;
                }
                for (std::size_t index = 0; index < expected->constant_members.size(); ++index)
                {
                    const ShaderMapBinding::ConstantMember& expected_member =
                        expected->constant_members[index];
                    const ShaderMapBinding::ConstantMember& reflected_member =
                        reflected.constant_members[index];
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
            stage_mask == rhi_enum_or(RHIShaderStageFlags::Vertex, RHIShaderStageFlags::Pixel);
        const bool compute = stage_mask == RHIShaderStageFlags::Compute;
        if ((!graphics && !compute) || program.stages.empty())
        {
            result.error = "ShaderMap program has an invalid graphics/compute stage set.";
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
                if (stage.stage == RHIShaderStage::Vertex) stage_flag = RHIShaderStageFlags::Vertex;
                else if (stage.stage == RHIShaderStage::Pixel) stage_flag = RHIShaderStageFlags::Pixel;
                else if (stage.stage == RHIShaderStage::Compute) stage_flag = RHIShaderStageFlags::Compute;
                if (!rhi_has_any_flag(binding.stages, stage_flag)) continue;
                const bool reflected = std::any_of(
                    stage.reflection.begin(), stage.reflection.end(),
                    [&](const ShaderMapBinding& value) {
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
}
