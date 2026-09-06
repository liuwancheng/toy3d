#include "rendercore/shader/view_uniform_shader_parameters.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/shader_uniform_buffer.h"

#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace toy3d
{
    namespace
    {
        RHIStatus write_matrix(const ShaderMapBinding::ConstantMember& member, const Matrix4& value,
                               std::vector<std::uint8_t>& bytes)
        {
            constexpr std::uint32_t k_matrix_byte_size =
                static_cast<std::uint32_t>(sizeof(float) * Matrix4::k_element_count);
            constexpr std::uint32_t k_matrix_column_stride =
                static_cast<std::uint32_t>(sizeof(float) * Matrix4::k_row_count);
            if (member.type != ShaderValueType::Float32x4x4 || member.size != k_matrix_byte_size ||
                member.array_stride != 0 || member.matrix_stride != k_matrix_column_stride ||
                member.offset > bytes.size() || member.size > bytes.size() - member.offset)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "View matrix member is incompatible with the ToyShaderABI");
            }
            std::memcpy(bytes.data() + member.offset, value.data(), k_matrix_byte_size);
            return RHIStatus::success();
        }

        RHIStatus write_vector3(const ShaderMapBinding::ConstantMember& member, const Vector3& value,
                                std::vector<std::uint8_t>& bytes)
        {
            constexpr std::uint32_t k_vector_byte_size = static_cast<std::uint32_t>(sizeof(float) * 3u);
            if (member.type != ShaderValueType::Float32x3 || member.size != k_vector_byte_size ||
                member.array_stride != 0 || member.matrix_stride != 0 || member.offset > bytes.size() ||
                member.size > bytes.size() - member.offset)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "View vector member is incompatible with the ToyShaderABI");
            }
            const float values[] = {value.x, value.y, value.z};
            std::memcpy(bytes.data() + member.offset, values, k_vector_byte_size);
            return RHIStatus::success();
        }

        RHIStatus write_view_member(const ShaderMapBinding::ConstantMember& member,
                                    const ViewUniformShaderParameters& parameters, std::vector<std::uint8_t>& bytes)
        {
            if (member.parameter_id != shader::make_shader_parameter_id(shader::BindingGroup::View,
                                                                        shader::ShaderParameterCategory::Constant,
                                                                        member.name))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "View constant member identity does not match its canonical name");
            }
            if (member.name == "toy_view")
            {
                return write_matrix(member, parameters.view_matrix, bytes);
            }
            if (member.name == "toy_projection")
            {
                return write_matrix(member, parameters.projection_matrix, bytes);
            }
            if (member.name == "toy_view_projection")
            {
                return write_matrix(member, parameters.view_projection_matrix, bytes);
            }
            if (member.name == "toy_inverse_view")
            {
                return write_matrix(member, parameters.inverse_view_matrix, bytes);
            }
            if (member.name == "toy_inverse_projection")
            {
                return write_matrix(member, parameters.inverse_projection_matrix, bytes);
            }
            if (member.name == "toy_inverse_view_projection")
            {
                return write_matrix(member, parameters.inverse_view_projection_matrix, bytes);
            }
            if (member.name == "toy_camera_position")
            {
                return write_vector3(member, parameters.camera_position, bytes);
            }
            if (member.name == "toy_camera_direction")
            {
                return write_vector3(member, parameters.camera_direction, bytes);
            }
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "View constant member has no canonical parameter source: " + member.name);
        }
    } // namespace

    RHIResult<std::shared_ptr<RHIBindingSet>> materialize_view_uniform_shader_parameters(
        RHIDevice& device, RHICommandContext& context, const std::shared_ptr<RHIBindingLayout>& binding_layout,
        const ShaderMapProgram& shader_program, const ViewUniformShaderParameters& parameters)
    {
        if (!binding_layout)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                RHIErrorCode::InvalidArgument, "View binding materialization requires an RHI binding layout");
        }

        const ShaderMapBinding* constant_buffer = nullptr;
        for (const ShaderMapBinding& binding : shader_program.data().bindings)
        {
            if (binding.group != RHIBindingGroup::View)
            {
                continue;
            }
            if (binding.type != RHIResourceBindingType::UniformBuffer)
            {
                return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                    RHIErrorCode::Unsupported, "View binding has no canonical resource-class source");
            }
            if (constant_buffer != nullptr)
            {
                return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                    RHIErrorCode::InvalidArgument, "View group must contain exactly one constant buffer");
            }
            constant_buffer = &binding;
        }

        if (constant_buffer == nullptr)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::success(nullptr);
        }
        if (constant_buffer->constant_buffer_size == 0 ||
            constant_buffer->constant_buffer_size >
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(RHIErrorCode::InvalidArgument,
                                                                      "View constant-buffer size is invalid");
        }

        std::vector<std::uint8_t> bytes(constant_buffer->constant_buffer_size, 0);
        for (const ShaderMapBinding::ConstantMember& member : constant_buffer->constant_members)
        {
            const RHIStatus status = write_view_member(member, parameters, bytes);
            if (!status)
            {
                return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(status.code(), status.message());
            }
        }

        RHIResult<RHIBufferRef> buffer = create_uploaded_shader_uniform_buffer(
            device, context, bytes,
            shader_program.data().shader_name + "/" + shader_program.data().pass_name + " ViewConstants");
        if (!buffer)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(buffer.status().code(),
                                                                      buffer.status().message());
        }

        RHIBindingSetDesc desc;
        desc.layout = binding_layout;
        desc.group = RHIBindingGroup::View;
        desc.debug_name = shader_program.data().shader_name + "/" + shader_program.data().pass_name + " ViewBindings";
        RHIBindingValue value;
        value.slot = constant_buffer->target_binding;
        value.buffer = std::move(buffer).value();
        value.buffer_size = constant_buffer->constant_buffer_size;
        desc.bindings.push_back(std::move(value));
        return device.create_binding_set(desc);
    }
} // namespace toy3d
