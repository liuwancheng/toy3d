#include "rendercore/shader/view_uniform_shader_parameters.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_device.h"
#include "format/shader_format_types.h"

namespace toy3d
{
    namespace
    {
        constexpr std::uint32_t k_view_uniform_byte_size = 416u;
        constexpr std::uint32_t k_matrix_byte_size = 64u;
        constexpr std::uint32_t k_matrix_stride = 16u;
        constexpr std::uint32_t k_vector3_byte_size = 12u;

        void write_bytes(std::vector<std::uint8_t>& bytes, std::uint32_t offset, const void* source,
                         std::uint32_t size)
        {
            std::memcpy(bytes.data() + offset, source, size);
        }

        ShaderDataLayoutHash view_data_layout_hash()
        {
            const std::vector<shader::ReflectedConstantMember> members = {
                {shader::make_shader_parameter_id(shader::BindingGroup::View,
                                                  shader::ShaderParameterCategory::Constant, "toy_view"),
                 "toy_view", shader::ShaderValueType::Float32x4x4, 0u, 64u, 0u, 16u},
                {shader::make_shader_parameter_id(shader::BindingGroup::View,
                                                  shader::ShaderParameterCategory::Constant, "toy_projection"),
                 "toy_projection", shader::ShaderValueType::Float32x4x4, 64u, 64u, 0u, 16u},
                {shader::make_shader_parameter_id(shader::BindingGroup::View,
                                                  shader::ShaderParameterCategory::Constant, "toy_view_projection"),
                 "toy_view_projection", shader::ShaderValueType::Float32x4x4, 128u, 64u, 0u, 16u},
                {shader::make_shader_parameter_id(shader::BindingGroup::View,
                                                  shader::ShaderParameterCategory::Constant, "toy_inverse_view"),
                 "toy_inverse_view", shader::ShaderValueType::Float32x4x4, 192u, 64u, 0u, 16u},
                {shader::make_shader_parameter_id(shader::BindingGroup::View,
                                                  shader::ShaderParameterCategory::Constant,
                                                  "toy_inverse_projection"),
                 "toy_inverse_projection", shader::ShaderValueType::Float32x4x4, 256u, 64u, 0u, 16u},
                {shader::make_shader_parameter_id(shader::BindingGroup::View,
                                                  shader::ShaderParameterCategory::Constant,
                                                  "toy_inverse_view_projection"),
                 "toy_inverse_view_projection", shader::ShaderValueType::Float32x4x4, 320u, 64u, 0u, 16u},
                {shader::make_shader_parameter_id(shader::BindingGroup::View,
                                                  shader::ShaderParameterCategory::Constant,
                                                  "toy_camera_position"),
                 "toy_camera_position", shader::ShaderValueType::Float32x3, 384u, 12u, 0u, 0u},
                {shader::make_shader_parameter_id(shader::BindingGroup::View,
                                                  shader::ShaderParameterCategory::Constant,
                                                  "toy_camera_direction"),
                 "toy_camera_direction", shader::ShaderValueType::Float32x3, 400u, 12u, 0u, 0u}};
            const ShaderParameterId binding_id = shader::make_shader_parameter_id(
                shader::BindingGroup::View, shader::ShaderParameterCategory::Constant, "");
            return shader::calculate_constant_buffer_data_layout_hash(
                shader::BindingGroup::View, binding_id, k_view_uniform_byte_size, members);
        }
    } // namespace

    RHIResult<RHIUniformBufferSlice> upload_view_uniform_shader_parameters(
        RHICommandContext& context, const ViewUniformShaderParameters& parameters)
    {
        std::vector<std::uint8_t> bytes(k_view_uniform_byte_size, 0u);
        write_bytes(bytes, 0u, parameters.view_matrix.data(), k_matrix_byte_size);
        write_bytes(bytes, 64u, parameters.projection_matrix.data(), k_matrix_byte_size);
        write_bytes(bytes, 128u, parameters.view_projection_matrix.data(), k_matrix_byte_size);
        write_bytes(bytes, 192u, parameters.inverse_view_matrix.data(), k_matrix_byte_size);
        write_bytes(bytes, 256u, parameters.inverse_projection_matrix.data(), k_matrix_byte_size);
        write_bytes(bytes, 320u, parameters.inverse_view_projection_matrix.data(), k_matrix_byte_size);
        const float camera_position[] = {parameters.camera_position.x, parameters.camera_position.y,
                                         parameters.camera_position.z};
        const float camera_direction[] = {parameters.camera_direction.x, parameters.camera_direction.y,
                                          parameters.camera_direction.z};
        write_bytes(bytes, 384u, camera_position, k_vector3_byte_size);
        write_bytes(bytes, 400u, camera_direction, k_vector3_byte_size);
        RHITransientUniformDataDesc desc;
        desc.source = {bytes.data(), bytes.size(), 0, 0};
        desc.data_layout_hash = view_data_layout_hash();
        desc.shader_abi_version = shader::toy_shader_abi_version;
        desc.debug_name = "ViewConstants";
        return context.upload_transient_uniform_data(desc);
    }

    RHIResult<std::shared_ptr<RHIBindingSet>> create_view_uniform_shader_binding(
        RHIDevice& device, const RHIUniformBufferSlice& slice)
    {
        if (!slice.buffer || !slice.buffer->is_owned_by(device) || slice.size != k_view_uniform_byte_size ||
            slice.data_layout_hash != view_data_layout_hash() ||
            slice.shader_abi_version != shader::toy_shader_abi_version)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                RHIErrorCode::InvalidArgument, "View binding requires the prepared canonical View uniform buffer");
        }

        RHIBindingSetDesc desc;
        desc.group = RHIBindingGroup::View;
        desc.debug_name = "ViewBindings";
        RHIBindingValue value;
        value.binding_id = shader::make_shader_parameter_id(
            shader::BindingGroup::View, shader::ShaderParameterCategory::Constant, "");
        value.buffer = slice.buffer;
        value.buffer_offset = slice.offset;
        value.buffer_size = slice.size;
        value.data_layout_hash = slice.data_layout_hash;
        value.shader_abi_version = slice.shader_abi_version;
        desc.bindings.push_back(std::move(value));
        return device.create_binding_set(desc);
    }
} // namespace toy3d
