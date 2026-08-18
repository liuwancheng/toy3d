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
                    result[word] |= static_cast<std::uint64_t>(
                        hash[word * sizeof(std::uint64_t) + byte]) << (byte * 8u);
                }
            }
            return result;
        }

        RHIShaderDesc make_shader_desc(
            const ShaderMapProgramData& program,
            const ShaderMapStage& stage)
        {
            RHIShaderDesc desc;
            desc.stage = stage.stage;
            desc.bytecode.bytes = stage.binary;
            switch (program.platform)
            {
            case ShaderPlatform::VulkanPortableV1: desc.bytecode.target = "spirv"; break;
            case ShaderPlatform::D3D11SM5: desc.bytecode.target = "dxbc"; break;
            case ShaderPlatform::D3D12SM6: desc.bytecode.target = "dxil"; break;
            }
            desc.entry_point = stage.entry_point;
            desc.content_hash = rhi_content_hash(stage.content_hash);
            desc.debug_name = program.shader_name + "/" + program.pass_name;
            for (const ShaderMapBinding& binding : stage.reflection)
            {
                desc.reflection.push_back({binding.name, binding.group,
                    binding.target_binding, binding.type, binding.array_count});
            }
            return desc;
        }
    }

    RHIResult<RHIShaderProgramDesc> build_rhi_shader_program_desc(
        const ShaderMapProgram& program)
    {
        const ShaderMapProgramData& data = program.data();

        RHIShaderProgramDesc result;
        result.binding_layout.debug_name = data.shader_name + "/" +
            data.pass_name + " BindingLayout";
        for (const ShaderMapBinding& binding : data.bindings)
        {
            result.binding_layout.entries.push_back({binding.group, binding.target_binding,
                binding.type, binding.stages, binding.array_count});
        }
        for (const ShaderMapStage& stage : data.stages)
        {
            RHIShaderDesc desc = make_shader_desc(data, stage);
            switch (stage.stage)
            {
            case RHIShaderStage::Vertex: result.vertex_shader = std::move(desc); break;
            case RHIShaderStage::Pixel: result.pixel_shader = std::move(desc); break;
            case RHIShaderStage::Compute: result.compute_shader = std::move(desc); break;
            default:
                return RHIResult<RHIShaderProgramDesc>::failure(
                    RHIErrorCode::Unsupported, "Shader Program stage is not supported by the RHI.");
            }
        }
        return RHIResult<RHIShaderProgramDesc>::success(std::move(result));
    }

    RHIResult<RHIShaderProgram> create_rhi_shader_program(
        RHIDevice& device,
        const ShaderMapProgram& program)
    {
        auto desc_result = build_rhi_shader_program_desc(program);
        if (!desc_result)
        {
            return RHIResult<RHIShaderProgram>::failure(
                desc_result.status().code(), desc_result.status().message());
        }
        RHIShaderProgramDesc desc = std::move(desc_result).value();
        RHIShaderProgram result;
        auto layout = device.create_binding_layout(desc.binding_layout);
        if (!layout)
        {
            return RHIResult<RHIShaderProgram>::failure(
                layout.status().code(), layout.status().message());
        }
        result.binding_layout = std::move(layout).value();

        const auto create_stage = [&](const std::optional<RHIShaderDesc>& shader_desc,
                                      RHIShaderRef& output) -> RHIStatus {
            if (!shader_desc) return RHIStatus::success();
            auto shader = device.create_shader(*shader_desc);
            if (!shader) return shader.status();
            output = std::move(shader).value();
            return RHIStatus::success();
        };
        RHIStatus status = create_stage(desc.vertex_shader, result.vertex_shader);
        if (status) status = create_stage(desc.pixel_shader, result.pixel_shader);
        if (status) status = create_stage(desc.compute_shader, result.compute_shader);
        if (!status)
        {
            return RHIResult<RHIShaderProgram>::failure(status.code(), status.message());
        }
        return RHIResult<RHIShaderProgram>::success(std::move(result));
    }
}
