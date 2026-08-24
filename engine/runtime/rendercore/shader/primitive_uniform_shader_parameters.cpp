#include "rendercore/shader/primitive_uniform_shader_parameters.h"

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
    RHIResult<std::shared_ptr<RHIBindingSet>>
    materialize_primitive_uniform_shader_parameters(
        RHIDevice& device,
        RHICommandContext& context,
        const std::shared_ptr<RHIBindingLayout>& binding_layout,
        const ShaderMapProgram& shader_program,
        const PrimitiveUniformShaderParameters& parameters)
    {
        if (!binding_layout)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                RHIErrorCode::InvalidArgument,
                "Object binding materialization requires an RHI binding layout");
        }

        const ShaderMapBinding* constant_buffer = nullptr;
        for (const ShaderMapBinding& binding : shader_program.data().bindings)
        {
            if (binding.group != RHIBindingGroup::Object)
            {
                continue;
            }
            if (binding.type != RHIResourceBindingType::UniformBuffer)
            {
                return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                    RHIErrorCode::Unsupported,
                    "Object binding has no canonical resource-class source");
            }
            if (constant_buffer != nullptr)
            {
                return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Object group must contain exactly one constant buffer");
            }
            constant_buffer = &binding;
        }

        if (constant_buffer == nullptr)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::success(nullptr);
        }
        constexpr std::uint32_t k_matrix_byte_size =
            static_cast<std::uint32_t>(
                sizeof(float) * Matrix4::k_element_count);
        constexpr std::uint32_t k_matrix_column_stride =
            static_cast<std::uint32_t>(
                sizeof(float) * Matrix4::k_row_count);
        if (constant_buffer->constant_buffer_size == 0 ||
            constant_buffer->constant_buffer_size >
                static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()))
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                RHIErrorCode::InvalidArgument,
                "Object constant-buffer size is invalid");
        }

        std::vector<std::uint8_t> bytes(
            constant_buffer->constant_buffer_size, 0);
        for (const ShaderMapBinding::ConstantMember& member :
             constant_buffer->constant_members)
        {
            if (member.parameter_id != shader::make_shader_parameter_id(
                    shader::BindingGroup::Object,
                    shader::ShaderParameterCategory::Constant,
                    member.name))
            {
                return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Object constant member identity does not match its canonical name");
            }
            if (member.name != "toy_object_to_world")
            {
                return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Object constant member has no canonical parameter source: " +
                        member.name);
            }
            if (member.type != ShaderValueType::Float32x4x4 ||
                member.size != k_matrix_byte_size ||
                member.array_stride != 0 ||
                member.matrix_stride != k_matrix_column_stride ||
                member.offset > bytes.size() ||
                member.size > bytes.size() - member.offset)
            {
                return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Object matrix member is incompatible with the ToyShaderABI");
            }
            std::memcpy(bytes.data() + member.offset,
                parameters.object_to_world.data(), k_matrix_byte_size);
        }

        RHIResult<RHIBufferRef> buffer =
            create_uploaded_shader_uniform_buffer(
            device, context, bytes,
            shader_program.data().shader_name + "/" +
                shader_program.data().pass_name + " ObjectConstants");
        if (!buffer)
        {
            return RHIResult<std::shared_ptr<RHIBindingSet>>::failure(
                buffer.status().code(), buffer.status().message());
        }

        RHIBindingSetDesc desc;
        desc.layout = binding_layout;
        desc.group = RHIBindingGroup::Object;
        desc.debug_name = shader_program.data().shader_name + "/" +
            shader_program.data().pass_name + " ObjectBindings";
        RHIBindingValue value;
        value.slot = constant_buffer->target_binding;
        value.buffer = std::move(buffer).value();
        value.buffer_size = constant_buffer->constant_buffer_size;
        desc.bindings.push_back(std::move(value));
        return device.create_binding_set(desc);
    }
}
