#include "rendercore/shader/primitive_uniform_shader_parameters.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "format/shader_format_types.h"

#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace toy3d
{
    RHIResult<std::shared_ptr<RHIBindingSet>> materialize_primitive_uniform_shader_parameters(
        RHIDevice& device, RHICommandContext& context, const PrimitiveUniformShaderParameters& parameters)
    {
        constexpr std::uint32_t k_matrix_byte_size =
            static_cast<std::uint32_t>(sizeof(float) * Matrix4::k_element_count);
        constexpr std::uint32_t k_matrix_column_stride =
            static_cast<std::uint32_t>(sizeof(float) * Matrix4::k_row_count);
        std::vector<std::uint8_t> bytes(k_matrix_byte_size, 0);
        std::memcpy(bytes.data(), parameters.object_to_world.data(), k_matrix_byte_size);
        const ShaderParameterId binding_id = shader::make_shader_parameter_id(
            shader::BindingGroup::Object, shader::ShaderParameterCategory::Constant, "");
        const std::vector<shader::ReflectedConstantMember> members = {
            {shader::make_shader_parameter_id(shader::BindingGroup::Object,
                                              shader::ShaderParameterCategory::Constant, "toy_object_to_world"),
             "toy_object_to_world", shader::ShaderValueType::Float32x4x4, 0u, k_matrix_byte_size, 0u,
             k_matrix_column_stride}};
        const ShaderDataLayoutHash data_layout_hash = shader::calculate_constant_buffer_data_layout_hash(
            shader::BindingGroup::Object, binding_id, k_matrix_byte_size, members);
        RHITransientUniformDataDesc upload_desc;
        upload_desc.source = {bytes.data(), bytes.size(), 0, 0};
        upload_desc.data_layout_hash = data_layout_hash;
        upload_desc.shader_abi_version = shader::toy_shader_abi_version;
        upload_desc.debug_name = "ObjectConstants";
        RHIResult<RHIUniformBufferSlice> slice = context.upload_transient_uniform_data(upload_desc);
        if (!slice)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(slice.status().code(), slice.status().message());
        }

        RHIBindingSetDesc desc;
        desc.group = RHIBindingGroup::Object;
        desc.debug_name = "ObjectBindings";
        RHIBindingValue value;
        value.binding_id = binding_id;
        value.buffer = slice.value().buffer;
        value.buffer_offset = slice.value().offset;
        value.buffer_size = slice.value().size;
        value.data_layout_hash = data_layout_hash;
        value.shader_abi_version = shader::toy_shader_abi_version;
        desc.bindings.push_back(std::move(value));
        return device.create_binding_set(desc);
    }
} // namespace toy3d
