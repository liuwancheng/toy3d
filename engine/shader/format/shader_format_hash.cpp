#include "format/shader_map_entry.h"

#include <algorithm>
#include <type_traits>
#include <vector>

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

        template<typename T>
        void append_enum(std::vector<std::uint8_t>& bytes, T value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value));
        }

        void append_string(std::vector<std::uint8_t>& bytes, const std::string& value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        std::vector<const ShaderCodeEntry*> sorted_stages(const ShaderMapEntry& entry)
        {
            std::vector<const ShaderCodeEntry*> stages;
            for (const ShaderCodeEntry& stage : entry.stages)
            {
                stages.push_back(&stage);
            }
            std::sort(stages.begin(), stages.end(), [](const auto* left, const auto* right) {
                return static_cast<std::uint32_t>(left->request.stage) <
                    static_cast<std::uint32_t>(right->request.stage);
            });
            return stages;
        }
    }

    ShaderStageFlags operator|(ShaderStageFlags left, ShaderStageFlags right)
    {
        return static_cast<ShaderStageFlags>(
            static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    ShaderStageFlags& operator|=(ShaderStageFlags& left, ShaderStageFlags right)
    {
        left = left | right;
        return left;
    }

    bool has_stage(ShaderStageFlags flags, ShaderStageFlags stage)
    {
        return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(stage)) != 0;
    }

    Sha256Hash calculate_target_binding_hash(
        ShaderTarget target,
        std::uint32_t mapping_version,
        const std::vector<ShaderMapBinding>& bindings)
    {
        std::vector<std::uint8_t> bytes;
        append_enum(bytes, target);
        append_integer(bytes, mapping_version);
        append_integer(bytes, static_cast<std::uint32_t>(bindings.size()));
        for (const ShaderMapBinding& binding : bindings)
        {
            append_integer(bytes, binding.binding_id);
            append_enum(bytes, binding.group);
            append_enum(bytes, binding.category);
            append_enum(bytes, binding.stages);
            append_enum(bytes, binding.register_class);
            append_integer(bytes, binding.register_index);
            append_integer(bytes, binding.descriptor_set);
            append_integer(bytes, binding.descriptor_binding);
        }
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_stage_reflection_hash(
        const ShaderStageReflection& reflection)
    {
        std::vector<std::uint8_t> bytes;
        append_enum(bytes, reflection.stage);
        append_string(bytes, reflection.entry_point);
        append_integer(bytes, static_cast<std::uint32_t>(reflection.bindings.size()));
        for (const ReflectedBinding& binding : reflection.bindings)
        {
            append_integer(bytes, binding.parameter_id);
            append_string(bytes, binding.name);
            append_enum(bytes, binding.group);
            append_enum(bytes, binding.category);
            append_integer(bytes, binding.resource_kind ?
                static_cast<std::uint32_t>(*binding.resource_kind) : 0xffffffffu);
            append_enum(bytes, binding.stages);
            append_integer(bytes, binding.array_count);
            append_integer(bytes, binding.descriptor_set);
            append_integer(bytes, binding.descriptor_binding);
            append_integer(bytes, binding.constant_buffer_size);
            append_integer(bytes, static_cast<std::uint32_t>(binding.constant_members.size()));
            for (const ReflectedConstantMember& member : binding.constant_members)
            {
                append_integer(bytes, member.parameter_id);
                append_string(bytes, member.name);
                append_enum(bytes, member.type);
                append_integer(bytes, member.offset);
                append_integer(bytes, member.size);
                append_integer(bytes, member.array_stride);
                append_integer(bytes, member.matrix_stride);
            }
        }
        append_integer(bytes, static_cast<std::uint32_t>(reflection.interface_variables.size()));
        for (const ReflectedInterfaceVariable& variable : reflection.interface_variables)
        {
            append_string(bytes, variable.name);
            append_string(bytes, variable.semantic);
            append_integer(bytes, variable.location);
            append_integer(bytes, variable.input ? 1u : 0u);
            append_enum(bytes, variable.scalar_type);
            append_integer(bytes, variable.component_count);
        }
        append_integer(bytes, reflection.thread_group_size_x);
        append_integer(bytes, reflection.thread_group_size_y);
        append_integer(bytes, reflection.thread_group_size_z);
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_map_key(const ShaderMapEntry& entry)
    {
        std::vector<std::uint8_t> bytes;
        append_integer(bytes, shader_map_entry_version);
        append_string(bytes, entry.shader_name);
        append_string(bytes, entry.pass_name);
        append_enum(bytes, entry.target);
        append_enum(bytes, entry.profile);
        append_integer(bytes, entry.mapping_version);
        bytes.insert(bytes.end(), entry.logical_layout_hash.begin(), entry.logical_layout_hash.end());
        bytes.insert(bytes.end(), entry.target_binding_hash.begin(), entry.target_binding_hash.end());
        bytes.insert(bytes.end(), entry.pass_template_hash.begin(), entry.pass_template_hash.end());
        append_integer(bytes, entry.variant_id_version);
        append_integer(bytes, entry.permutation_version);
        bytes.insert(bytes.end(), entry.permutation_key.begin(), entry.permutation_key.end());
        for (const ShaderCodeEntry* stage : sorted_stages(entry))
        {
            append_enum(bytes, stage->request.stage);
            bytes.insert(bytes.end(), stage->request.compile_key.begin(),
                stage->request.compile_key.end());
            bytes.insert(bytes.end(), stage->reflection.reflection_hash.begin(),
                stage->reflection.reflection_hash.end());
            const Sha256Hash binary_hash = sha256(stage->binary);
            bytes.insert(bytes.end(), binary_hash.begin(), binary_hash.end());
        }
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_map_entry_content_hash(const ShaderMapEntry& entry)
    {
        std::vector<std::uint8_t> bytes;
        const Sha256Hash key = calculate_shader_map_key(entry);
        bytes.insert(bytes.end(), key.begin(), key.end());
        append_integer(bytes, static_cast<std::uint32_t>(entry.bindings.size()));
        for (const ShaderMapBinding& binding : entry.bindings)
        {
            append_integer(bytes, binding.binding_id);
            append_string(bytes, binding.name);
            append_enum(bytes, binding.group);
            append_enum(bytes, binding.category);
            append_enum(bytes, binding.stages);
            append_enum(bytes, binding.register_class);
            append_integer(bytes, binding.register_index);
            append_integer(bytes, binding.descriptor_set);
            append_integer(bytes, binding.descriptor_binding);
        }
        for (const ShaderCodeEntry* stage : sorted_stages(entry))
        {
            append_integer(bytes, static_cast<std::uint32_t>(stage->request.dependencies.size()));
            for (const ShaderDependency& dependency : stage->request.dependencies)
            {
                append_string(bytes, dependency.virtual_path);
                bytes.insert(bytes.end(), dependency.content_hash.begin(),
                    dependency.content_hash.end());
            }
        }
        return sha256(bytes);
    }
}
