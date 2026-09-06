#include "rendercore/shader/rhi_shader_program.h"

#include <utility>

namespace toy3d
{
    namespace
    {
        std::array<std::uint64_t, 2> rhi_content_hash(const ShaderContentHash& hash)
        {
            std::array<std::uint64_t, 2> result{};
            for (std::size_t word = 0; word < result.size(); ++word)
            {
                for (std::size_t byte = 0; byte < sizeof(std::uint64_t); ++byte)
                {
                    result[word] |= static_cast<std::uint64_t>(hash[word * sizeof(std::uint64_t) + byte])
                                    << (byte * 8u);
                }
            }
            return result;
        }

        RHIShaderVertexInputReflection::ScalarType rhi_scalar_type(
            shader::ReflectedInterfaceVariable::ScalarType scalar_type)
        {
            switch (scalar_type)
            {
            case shader::ReflectedInterfaceVariable::ScalarType::Float32:
                return RHIShaderVertexInputReflection::ScalarType::Float32;
            case shader::ReflectedInterfaceVariable::ScalarType::Int32:
                return RHIShaderVertexInputReflection::ScalarType::Int32;
            case shader::ReflectedInterfaceVariable::ScalarType::UInt32:
                return RHIShaderVertexInputReflection::ScalarType::UInt32;
            }
            return RHIShaderVertexInputReflection::ScalarType::Float32;
        }

        RHIShaderDesc make_shader_desc(const ShaderMapProgramData& program, const ShaderMapStage& stage)
        {
            RHIShaderDesc desc;
            desc.stage = stage.stage;
            desc.bytecode.bytes = stage.binary;
            switch (program.platform)
            {
            case ShaderPlatform::VulkanES31:
                desc.bytecode.target = "spirv";
                break;
            case ShaderPlatform::D3D11SM5:
                desc.bytecode.target = "dxbc";
                break;
            case ShaderPlatform::D3D12SM6:
                desc.bytecode.target = "dxil";
                break;
            }
            desc.entry_point = stage.entry_point;
            desc.content_hash = rhi_content_hash(stage.content_hash);
            desc.debug_name = program.shader_name + "/" + program.pass_name;
            for (const ShaderMapBinding& binding : stage.reflection)
            {
                desc.reflection.push_back(
                    {binding.name, binding.group, binding.target_binding, binding.type, binding.array_count});
            }
            if (stage.stage == RHIShaderStage::Vertex)
            {
                for (const ShaderVertexInput& input : program.vertex_inputs)
                {
                    desc.vertex_inputs.push_back({input.semantic_name, input.semantic_index, input.target_location,
                                                  rhi_scalar_type(input.scalar_type), input.component_count});
                }
            }
            return desc;
        }
    } // namespace

    RHIResult<RHIShaderProgramDesc> build_rhi_shader_program_desc(const ShaderMapProgram& program)
    {
        const ShaderMapProgramData& data = program.data();

        RHIShaderProgramDesc result;
        result.binding_layout.debug_name = data.shader_name + "/" + data.pass_name + " BindingLayout";
        for (const ShaderMapBinding& binding : data.bindings)
        {
            result.binding_layout.entries.push_back(
                {binding.group, binding.target_binding, binding.type, binding.stages, binding.array_count});
        }
        for (const ShaderMapStage& stage : data.stages)
        {
            RHIShaderDesc desc = make_shader_desc(data, stage);
            switch (stage.stage)
            {
            case RHIShaderStage::Vertex:
                result.vertex_shader = std::move(desc);
                break;
            case RHIShaderStage::Pixel:
                result.pixel_shader = std::move(desc);
                break;
            case RHIShaderStage::Compute:
                result.compute_shader = std::move(desc);
                break;
            default:
                return RHIResult<RHIShaderProgramDesc>::failure(RHIErrorCode::Unsupported,
                                                                "Shader Program stage is not supported by the RHI.");
            }
        }
        return RHIResult<RHIShaderProgramDesc>::success(std::move(result));
    }

} // namespace toy3d
