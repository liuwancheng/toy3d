#include "rendercore/shader/view_uniform_shader_parameters.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_device.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/shader_uniform_buffer.h"

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

        bool member_matches(const ShaderMapBinding::ConstantMember& member, const char* name,
                            ShaderValueType type, std::uint32_t offset, std::uint32_t size,
                            std::uint32_t matrix_stride)
        {
            return member.name == name &&
                   member.parameter_id == shader::make_shader_parameter_id(
                                              shader::BindingGroup::View,
                                              shader::ShaderParameterCategory::Constant, name) &&
                   member.type == type && member.offset == offset && member.size == size &&
                   member.array_stride == 0u && member.matrix_stride == matrix_stride;
        }

        RHIStatus validate_view_constant_buffer(const ShaderMapBinding& binding)
        {
            if (binding.type != RHIResourceBindingType::UniformBuffer ||
                binding.constant_buffer_size != k_view_uniform_byte_size || binding.constant_members.size() != 8u)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "View constant buffer does not match the canonical ToyShaderABI layout");
            }

            const std::vector<ShaderMapBinding::ConstantMember>& members = binding.constant_members;
            const bool matches =
                member_matches(members[0], "toy_view", ShaderValueType::Float32x4x4, 0u, k_matrix_byte_size,
                               k_matrix_stride) &&
                member_matches(members[1], "toy_projection", ShaderValueType::Float32x4x4, 64u,
                               k_matrix_byte_size, k_matrix_stride) &&
                member_matches(members[2], "toy_view_projection", ShaderValueType::Float32x4x4, 128u,
                               k_matrix_byte_size, k_matrix_stride) &&
                member_matches(members[3], "toy_inverse_view", ShaderValueType::Float32x4x4, 192u,
                               k_matrix_byte_size, k_matrix_stride) &&
                member_matches(members[4], "toy_inverse_projection", ShaderValueType::Float32x4x4, 256u,
                               k_matrix_byte_size, k_matrix_stride) &&
                member_matches(members[5], "toy_inverse_view_projection", ShaderValueType::Float32x4x4, 320u,
                               k_matrix_byte_size, k_matrix_stride) &&
                member_matches(members[6], "toy_camera_position", ShaderValueType::Float32x3, 384u,
                               k_vector3_byte_size, 0u) &&
                member_matches(members[7], "toy_camera_direction", ShaderValueType::Float32x3, 400u,
                               k_vector3_byte_size, 0u);
            return matches ? RHIStatus::success()
                           : RHIStatus::failure(
                                 RHIErrorCode::InvalidArgument,
                                 "View constant members do not match the canonical ToyShaderABI layout");
        }

        RHIResult<const ShaderMapBinding*> find_view_constant_buffer(const ShaderMapProgram& shader_program)
        {
            const ShaderMapBinding* constant_buffer = nullptr;
            for (const ShaderMapBinding& binding : shader_program.data().bindings)
            {
                if (binding.group != RHIBindingGroup::View)
                {
                    continue;
                }
                if (binding.type != RHIResourceBindingType::UniformBuffer || constant_buffer != nullptr)
                {
                    return RHIResult<const ShaderMapBinding*>::failure(
                        binding.type == RHIResourceBindingType::UniformBuffer ? RHIErrorCode::InvalidArgument
                                                                             : RHIErrorCode::Unsupported,
                        "View group must contain exactly one canonical uniform buffer");
                }
                constant_buffer = &binding;
            }
            if (constant_buffer == nullptr)
            {
                return RHIResult<const ShaderMapBinding*>::success(nullptr);
            }
            const RHIStatus status = validate_view_constant_buffer(*constant_buffer);
            if (!status)
            {
                return RHIResult<const ShaderMapBinding*>::failure(status.code(), status.message());
            }
            return RHIResult<const ShaderMapBinding*>::success(constant_buffer);
        }
    } // namespace

    RHIResult<std::shared_ptr<RHIBuffer>> create_view_uniform_shader_buffer(
        RHIDevice& device, RHICommandContext& context, const ViewUniformShaderParameters& parameters)
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
        return create_uploaded_shader_uniform_buffer(device, context, bytes, "ViewConstants");
    }

    RHIStatus validate_view_uniform_shader_program(const ShaderMapProgram& shader_program)
    {
        const RHIResult<const ShaderMapBinding*> constant_buffer = find_view_constant_buffer(shader_program);
        return constant_buffer ? RHIStatus::success() : constant_buffer.status();
    }

    RHIResult<std::shared_ptr<RHIBindingSet>> create_view_uniform_shader_binding(
        RHIDevice& device, const std::shared_ptr<RHIBindingLayout>& binding_layout,
        const ShaderMapProgram& shader_program, const std::shared_ptr<RHIBuffer>& buffer)
    {
        if (!binding_layout)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                RHIErrorCode::InvalidArgument, "View binding creation requires an RHI binding layout");
        }

        RHIResult<const ShaderMapBinding*> constant_buffer = find_view_constant_buffer(shader_program);
        if (!constant_buffer)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(constant_buffer.status().code(),
                                                                      constant_buffer.status().message());
        }
        if (constant_buffer.value() == nullptr)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::success(nullptr);
        }
        if (!buffer || !buffer->is_owned_by(device) || buffer->desc().size != k_view_uniform_byte_size)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                RHIErrorCode::InvalidArgument, "View binding requires the prepared canonical View uniform buffer");
        }

        RHIBindingSetDesc desc;
        desc.layout = binding_layout;
        desc.group = RHIBindingGroup::View;
        desc.debug_name = shader_program.data().shader_name + "/" + shader_program.data().pass_name + " ViewBindings";
        RHIBindingValue value;
        value.slot = constant_buffer.value()->target_binding;
        value.buffer = buffer;
        value.buffer_size = k_view_uniform_byte_size;
        desc.bindings.push_back(std::move(value));
        return device.create_binding_set(desc);
    }
} // namespace toy3d
