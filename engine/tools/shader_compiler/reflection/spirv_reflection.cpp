#include "reflection/spirv_reflection.h"

#include <spirv_reflect.h>

#include <algorithm>

namespace toy3d::shader
{
    // Reflection helpers use optional because unsupported native kinds and
    // invalid members must not be converted into fabricated public enum values.
    namespace
    {
        class ReflectionModule
        {
        public:
            ~ReflectionModule()
            {
                if (created) spvReflectDestroyShaderModule(&module);
            }

            SpvReflectShaderModule module{};
            bool created = false;
        };

        void add_error(
            std::vector<Diagnostic>& diagnostics,
            DiagnosticCode code,
            const ShaderCompileRequest& request,
            const std::string& message)
        {
            diagnostics.push_back({DiagnosticSeverity::Error, code,
                {request.source_virtual_path, 0, 1, 1}, message});
        }

        ShaderStageFlags reflected_stage(SpvReflectShaderStageFlagBits stage)
        {
            switch (stage)
            {
            case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT: return ShaderStageFlags::Vertex;
            case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT: return ShaderStageFlags::Pixel;
            case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT: return ShaderStageFlags::Compute;
            default: return ShaderStageFlags::None;
            }
        }

        ShaderParameterCategory reflected_category(const SpvReflectDescriptorBinding& binding)
        {
            if ((binding.resource_type & SPV_REFLECT_RESOURCE_FLAG_CBV) != 0u)
                return ShaderParameterCategory::Constant;
            if ((binding.resource_type & SPV_REFLECT_RESOURCE_FLAG_SAMPLER) != 0u)
                return ShaderParameterCategory::Sampler;
            if ((binding.resource_type & SPV_REFLECT_RESOURCE_FLAG_UAV) != 0u)
            {
                return binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE ?
                    ShaderParameterCategory::StorageTexture : ShaderParameterCategory::StorageBuffer;
            }
            return binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE ?
                ShaderParameterCategory::SampledTexture : ShaderParameterCategory::ReadOnlyBuffer;
        }

        bool descriptor_matches_category(
            const SpvReflectDescriptorBinding& reflected,
            ShaderParameterCategory expected)
        {
            switch (expected)
            {
            case ShaderParameterCategory::Constant:
                return reflected.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case ShaderParameterCategory::SampledTexture:
                return reflected.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case ShaderParameterCategory::Sampler:
                return reflected.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER;
            case ShaderParameterCategory::ReadOnlyBuffer:
                return reflected.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
                    (reflected.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
                     (reflected.resource_type & SPV_REFLECT_RESOURCE_FLAG_SRV) != 0u);
            case ShaderParameterCategory::StorageBuffer:
                return reflected.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
                    (reflected.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
                     (reflected.resource_type & SPV_REFLECT_RESOURCE_FLAG_UAV) != 0u);
            case ShaderParameterCategory::StorageTexture:
                return reflected.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }
            return false;
        }

        std::optional<ResourceKind> reflected_resource_kind(
            const SpvReflectDescriptorBinding& binding,
            ShaderParameterCategory category)
        {
            if (category == ShaderParameterCategory::Sampler) return ResourceKind::Sampler;
            if (binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)
                return ResourceKind::Buffer;
            if (binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
                return ResourceKind::RWBuffer;
            if (binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            {
                const bool writable = category == ShaderParameterCategory::StorageBuffer;
                switch (binding.user_type)
                {
                case SPV_REFLECT_USER_TYPE_BYTE_ADDRESS_BUFFER:
                case SPV_REFLECT_USER_TYPE_RW_BYTE_ADDRESS_BUFFER:
                    return writable ? ResourceKind::RWByteAddressBuffer : ResourceKind::ByteAddressBuffer;
                case SPV_REFLECT_USER_TYPE_STRUCTURED_BUFFER:
                case SPV_REFLECT_USER_TYPE_RW_STRUCTURED_BUFFER:
                    return writable ? ResourceKind::RWStructuredBuffer : ResourceKind::StructuredBuffer;
                default: return std::nullopt;
                }
            }
            const bool storage = category == ShaderParameterCategory::StorageTexture;
            switch (binding.image.dim)
            {
            case SpvDim2D:
                if (binding.image.ms != 0u && binding.image.arrayed == 0u && !storage)
                    return ResourceKind::Texture2DMS;
                if (binding.image.ms != 0u) return std::nullopt;
                if (binding.image.arrayed != 0u)
                    return storage ? ResourceKind::RWTexture2DArray : ResourceKind::Texture2DArray;
                return storage ? ResourceKind::RWTexture2D : ResourceKind::Texture2D;
            case SpvDim3D:
                return storage ? ResourceKind::RWTexture3D : ResourceKind::Texture3D;
            case SpvDimCube:
                return storage || binding.image.arrayed != 0u ? std::nullopt :
                    std::optional<ResourceKind>(ResourceKind::TextureCube);
            default: return std::nullopt;
            }
        }

        std::optional<ShaderValueType> reflected_value_type(const SpvReflectBlockVariable& member)
        {
            if (!member.type_description || member.numeric.scalar.width != 32u) return std::nullopt;
            const std::uint32_t columns = std::max(1u, member.numeric.matrix.column_count);
            const std::uint32_t rows = member.numeric.matrix.column_count == 0u ?
                std::max(1u, member.numeric.vector.component_count) : member.numeric.matrix.row_count;
            const bool is_float = (member.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0u;
            if (!is_float && columns != 1u) return std::nullopt;
            if (is_float)
            {
                if (columns == 1u) return static_cast<ShaderValueType>(
                    static_cast<int>(ShaderValueType::Float32) + static_cast<int>(rows - 1u));
                if (columns >= 2u && columns <= 4u && rows >= 2u && rows <= 4u)
                {
                    const int matrix_index = static_cast<int>((rows - 2u) * 3u + (columns - 2u));
                    return static_cast<ShaderValueType>(static_cast<int>(ShaderValueType::Float32x2x2) + matrix_index);
                }
                return std::nullopt;
            }
            const ShaderValueType base = member.numeric.scalar.signedness != 0u ?
                ShaderValueType::Int32 : ShaderValueType::UInt32;
            if (rows > 4u) return std::nullopt;
            return static_cast<ShaderValueType>(static_cast<int>(base) + static_cast<int>(rows - 1u));
        }

    }

    bool SpirvReflectionResult::succeeded() const
    {
        return reflection.has_value() && diagnostics.empty();
    }

    SpirvReflectionResult reflect_and_validate_spirv(
        const std::vector<std::uint8_t>& binary,
        const ShaderCompileRequest& request,
        const TargetBindingLayout& expected_layout,
        bool require_all_expected_bindings)
    {
        SpirvReflectionResult result;
        if (request.target != ShaderTarget::VulkanSpirV ||
            request.profile != ShaderCompileProfile::VulkanPortableV1 ||
            expected_layout.target != ShaderTarget::VulkanSpirV ||
            expected_layout.mapping_version != vulkan_binding_mapping_version ||
            expected_layout.target_binding_hash != request.target_binding_hash)
        {
            add_error(result.diagnostics, DiagnosticCode::ReflectionMismatch, request,
                "SPIR-V reflection requires the current VulkanPortable mapping version.");
            return result;
        }

        ReflectionModule reflected_module;
        const SpvReflectResult create_result = spvReflectCreateShaderModule(
            binary.size(), binary.data(), &reflected_module.module);
        if (create_result != SPV_REFLECT_RESULT_SUCCESS)
        {
            add_error(result.diagnostics, DiagnosticCode::ReflectionFailed, request,
                "SPIRV-Reflect could not parse the final module (result " +
                    std::to_string(static_cast<int>(create_result)) + ").");
            return result;
        }
        reflected_module.created = true;
        ShaderStageReflection reflection;
        reflection.stage = reflected_stage(reflected_module.module.shader_stage);
        reflection.entry_point = reflected_module.module.entry_point_name ?
            reflected_module.module.entry_point_name : std::string{};
        if (reflection.stage != request.stage || reflection.entry_point != request.entry_point)
        {
            add_error(result.diagnostics, DiagnosticCode::ReflectionMismatch, request,
                "Reflected stage or entry point does not match the compile request.");
        }

        std::uint32_t descriptor_count = 0;
        if (spvReflectEnumerateDescriptorBindings(&reflected_module.module, &descriptor_count, nullptr) !=
            SPV_REFLECT_RESULT_SUCCESS)
        {
            add_error(result.diagnostics, DiagnosticCode::ReflectionFailed, request,
                "SPIRV-Reflect failed to enumerate descriptor bindings.");
            return result;
        }
        std::vector<SpvReflectDescriptorBinding*> descriptors(descriptor_count);
        if (descriptor_count != 0u &&
            spvReflectEnumerateDescriptorBindings(&reflected_module.module, &descriptor_count, descriptors.data()) !=
                SPV_REFLECT_RESULT_SUCCESS)
        {
            add_error(result.diagnostics, DiagnosticCode::ReflectionFailed, request,
                "SPIRV-Reflect failed to read descriptor bindings.");
            return result;
        }
        std::sort(descriptors.begin(), descriptors.end(), [](const auto* left, const auto* right) {
            return left->set != right->set ? left->set < right->set : left->binding < right->binding;
        });
        for (const SpvReflectDescriptorBinding* descriptor : descriptors)
        {
            const auto expected = std::find_if(expected_layout.bindings.begin(), expected_layout.bindings.end(),
                [&](const NativeBinding& binding) {
                    return has_stage(binding.stages, request.stage) &&
                        binding.descriptor_set == descriptor->set &&
                        binding.descriptor_binding == descriptor->binding;
                });
            if (expected == expected_layout.bindings.end())
            {
                add_error(result.diagnostics, DiagnosticCode::ReflectionUnexpectedResource, request,
                    "SPIR-V contains unexpected resource '" +
                        std::string(descriptor->name ? descriptor->name : "<unnamed>") + "' at set " +
                        std::to_string(descriptor->set) + ", binding " + std::to_string(descriptor->binding) + ".");
                continue;
            }
            const std::string reflected_name = descriptor->name ? descriptor->name : std::string{};
            if (reflected_name != expected->name || !descriptor_matches_category(*descriptor, expected->category) ||
                descriptor->count != 1u)
            {
                add_error(result.diagnostics, DiagnosticCode::ReflectionMismatch, request,
                    "Reflected identity, type, or array count differs for expected binding '" + expected->name + "'.");
                continue;
            }
            const std::optional<ResourceKind> native_resource_kind =
                reflected_resource_kind(*descriptor, expected->category);
            if (expected->category != ShaderParameterCategory::Constant &&
                (!native_resource_kind ||
                 (expected->logical_binding && expected->logical_binding->resource &&
                  expected->logical_binding->resource->resource_kind != *native_resource_kind &&
                  !(expected->category == ShaderParameterCategory::Sampler &&
                    (expected->logical_binding->resource->resource_kind == ResourceKind::Sampler ||
                     expected->logical_binding->resource->resource_kind == ResourceKind::ComparisonSampler)))))
            {
                add_error(result.diagnostics, DiagnosticCode::ReflectionMismatch, request,
                    "Reflected resource kind differs for expected binding '" + expected->name + "'.");
                continue;
            }
            ReflectedBinding binding;
            binding.parameter_id = expected->binding_id;
            binding.name = reflected_name;
            binding.group = expected->group;
            binding.category = reflected_category(*descriptor);
            binding.resource_kind = native_resource_kind;
            binding.stages = request.stage;
            binding.array_count = descriptor->count;
            binding.descriptor_set = descriptor->set;
            binding.descriptor_binding = descriptor->binding;
            if (expected->logical_binding && expected->logical_binding->constant_buffer)
            {
                const ConstantBufferLayout& expected_buffer = *expected->logical_binding->constant_buffer;
                binding.category = ShaderParameterCategory::Constant;
                binding.constant_buffer_size = descriptor->block.padded_size;
                if (binding.constant_buffer_size != expected_buffer.size ||
                    descriptor->block.member_count != expected_buffer.members.size())
                {
                    add_error(result.diagnostics, DiagnosticCode::ReflectionMismatch, request,
                        "Reflected constant-buffer size or member count differs for '" + expected->name + "'.");
                    continue;
                }
                for (std::uint32_t index = 0; index < descriptor->block.member_count; ++index)
                {
                    const SpvReflectBlockVariable& member = descriptor->block.members[index];
                    const ShaderConstantMember& expected_member = expected_buffer.members[index];
                    const auto type = reflected_value_type(member);
                    const std::string member_name = member.name ? member.name : std::string{};
                    if (!type || member_name != expected_member.name || *type != expected_member.type ||
                        member.offset != expected_member.offset ||
                        member.array.stride != expected_member.array_stride ||
                        member.numeric.matrix.stride != expected_member.matrix_stride)
                    {
                        add_error(result.diagnostics, DiagnosticCode::ReflectionMismatch, request,
                            "Reflected constant member layout differs for '" + expected->name + "." +
                                expected_member.name + "'.");
                        continue;
                    }
                    binding.constant_members.push_back({expected_member.parameter_id, member_name, *type,
                        member.offset, member.size, member.array.stride, member.numeric.matrix.stride});
                }
            }
            else if (expected->logical_binding && expected->logical_binding->resource)
            {
                binding.resource_kind = expected->logical_binding->resource->resource_kind;
            }
            reflection.bindings.push_back(std::move(binding));
        }
        if (require_all_expected_bindings)
        {
            for (const NativeBinding& expected : expected_layout.bindings)
            {
                if (!has_stage(expected.stages, request.stage)) continue;
                const bool found = std::any_of(reflection.bindings.begin(), reflection.bindings.end(),
                    [&](const ReflectedBinding& binding) {
                        return binding.descriptor_set == expected.descriptor_set &&
                            binding.descriptor_binding == expected.descriptor_binding;
                    });
                if (!found)
                {
                    add_error(result.diagnostics, DiagnosticCode::ReflectionMismatch, request,
                        "Expected active binding '" + expected.name + "' is absent from final SPIR-V.");
                }
            }
        }

        const auto append_interfaces = [&](SpvReflectInterfaceVariable** variables, std::uint32_t count, bool input) {
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const SpvReflectInterfaceVariable& variable = *variables[index];
                if (variable.built_in >= 0) continue;
                if (variable.numeric.scalar.width != 32u ||
                    (variable.decoration_flags & SPV_REFLECT_DECORATION_RELAXED_PRECISION) != 0u)
                {
                    add_error(result.diagnostics, DiagnosticCode::ShaderInterfacePrecisionMismatch, request,
                        "Shader interface variable '" + std::string(variable.name ? variable.name : "<unnamed>") +
                            "' is not guaranteed to use the public 32-bit interface ABI.");
                    continue;
                }
                ReflectedInterfaceVariable reflected;
                reflected.name = variable.name ? variable.name : std::string{};
                reflected.semantic = variable.semantic ? variable.semantic : std::string{};
                reflected.location = variable.location;
                reflected.input = input;
                reflected.component_count = variable.numeric.vector.component_count == 0u ?
                    1u : variable.numeric.vector.component_count;
                if (variable.type_description &&
                    (variable.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_INT) != 0u)
                {
                    reflected.scalar_type = variable.numeric.scalar.signedness != 0u ?
                        ReflectedInterfaceVariable::ScalarType::Int32 :
                        ReflectedInterfaceVariable::ScalarType::UInt32;
                }
                reflection.interface_variables.push_back(std::move(reflected));
            }
        };
        append_interfaces(reflected_module.module.input_variables,
            reflected_module.module.input_variable_count, true);
        append_interfaces(reflected_module.module.output_variables,
            reflected_module.module.output_variable_count, false);
        if (request.stage == ShaderStageFlags::Compute && reflected_module.module.entry_point_count != 0u)
        {
            const SpvReflectEntryPoint& entry = reflected_module.module.entry_points[0];
            reflection.thread_group_size_x = entry.local_size.x;
            reflection.thread_group_size_y = entry.local_size.y;
            reflection.thread_group_size_z = entry.local_size.z;
        }
        std::sort(reflection.interface_variables.begin(), reflection.interface_variables.end(),
            [](const ReflectedInterfaceVariable& left, const ReflectedInterfaceVariable& right) {
                return left.input != right.input ? left.input > right.input : left.location < right.location;
            });
        if (!result.diagnostics.empty()) return result;
        reflection.reflection_hash = calculate_shader_stage_reflection_hash(reflection);
        result.reflection = std::move(reflection);
        return result;
    }
}
