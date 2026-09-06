#include "rendercore/shader/shader_graphics_state.h"

#include <cstdint>
#include <string>
#include <utility>

namespace toy3d
{
    namespace
    {
        RHIResult<RHIGraphicsPipelineDesc> invalid_state(
            const RHIGraphicsPipelineDesc& desc,
            const char* field)
        {
            const std::string identity = desc.debug_name.empty()
                ? std::string("<unnamed shader pass>")
                : desc.debug_name;
            return RHIResult<RHIGraphicsPipelineDesc>::failure(
                RHIErrorCode::InvalidArgument,
                "Shader graphics state conversion failed for " + identity +
                    ": invalid " + field + ".");
        }

        bool convert_primitive_topology(
            shader::ShaderGraphicsPassState::PrimitiveTopology source,
            RHIPrimitiveTopology& destination)
        {
            using Source = shader::ShaderGraphicsPassState::PrimitiveTopology;
            switch (source)
            {
            case Source::PointList: destination = RHIPrimitiveTopology::PointList; return true;
            case Source::LineList: destination = RHIPrimitiveTopology::LineList; return true;
            case Source::LineStrip: destination = RHIPrimitiveTopology::LineStrip; return true;
            case Source::TriangleList: destination = RHIPrimitiveTopology::TriangleList; return true;
            case Source::TriangleStrip: destination = RHIPrimitiveTopology::TriangleStrip; return true;
            }
            return false;
        }

        bool convert_cull_mode(
            shader::ShaderGraphicsPassState::CullMode source,
            RHICullMode& destination)
        {
            using Source = shader::ShaderGraphicsPassState::CullMode;
            switch (source)
            {
            case Source::None: destination = RHICullMode::None; return true;
            case Source::Front: destination = RHICullMode::Front; return true;
            case Source::Back: destination = RHICullMode::Back; return true;
            }
            return false;
        }

        bool convert_front_face(
            shader::ShaderGraphicsPassState::FrontFace source,
            RHIFrontFace& destination)
        {
            using Source = shader::ShaderGraphicsPassState::FrontFace;
            switch (source)
            {
            case Source::Clockwise: destination = RHIFrontFace::Clockwise; return true;
            case Source::CounterClockwise: destination = RHIFrontFace::CounterClockwise; return true;
            }
            return false;
        }

        bool convert_fill_mode(
            shader::ShaderGraphicsPassState::FillMode source,
            RHIPolygonMode& destination)
        {
            using Source = shader::ShaderGraphicsPassState::FillMode;
            switch (source)
            {
            case Source::Solid: destination = RHIPolygonMode::Fill; return true;
            case Source::Wireframe: destination = RHIPolygonMode::Line; return true;
            }
            return false;
        }

        bool convert_compare_operation(
            shader::ShaderGraphicsPassState::CompareOperation source,
            RHICompareOperation& destination)
        {
            using Source = shader::ShaderGraphicsPassState::CompareOperation;
            switch (source)
            {
            case Source::Never: destination = RHICompareOperation::Never; return true;
            case Source::Less: destination = RHICompareOperation::Less; return true;
            case Source::Equal: destination = RHICompareOperation::Equal; return true;
            case Source::LessEqual: destination = RHICompareOperation::LessEqual; return true;
            case Source::Greater: destination = RHICompareOperation::Greater; return true;
            case Source::NotEqual: destination = RHICompareOperation::NotEqual; return true;
            case Source::GreaterEqual: destination = RHICompareOperation::GreaterEqual; return true;
            case Source::Always: destination = RHICompareOperation::Always; return true;
            }
            return false;
        }

        bool convert_stencil_operation(
            shader::ShaderGraphicsPassState::StencilOperation source,
            RHIStencilOperation& destination)
        {
            using Source = shader::ShaderGraphicsPassState::StencilOperation;
            switch (source)
            {
            case Source::Keep: destination = RHIStencilOperation::Keep; return true;
            case Source::Zero: destination = RHIStencilOperation::Zero; return true;
            case Source::Replace: destination = RHIStencilOperation::Replace; return true;
            case Source::IncrementClamp: destination = RHIStencilOperation::IncrementClamp; return true;
            case Source::DecrementClamp: destination = RHIStencilOperation::DecrementClamp; return true;
            case Source::Invert: destination = RHIStencilOperation::Invert; return true;
            case Source::IncrementWrap: destination = RHIStencilOperation::IncrementWrap; return true;
            case Source::DecrementWrap: destination = RHIStencilOperation::DecrementWrap; return true;
            }
            return false;
        }

        bool convert_blend_factor(
            shader::ShaderGraphicsPassState::BlendFactor source,
            RHIBlendFactor& destination)
        {
            using Source = shader::ShaderGraphicsPassState::BlendFactor;
            switch (source)
            {
            case Source::Zero: destination = RHIBlendFactor::Zero; return true;
            case Source::One: destination = RHIBlendFactor::One; return true;
            case Source::SourceColor: destination = RHIBlendFactor::SourceColor; return true;
            case Source::OneMinusSourceColor: destination = RHIBlendFactor::OneMinusSourceColor; return true;
            case Source::DestinationColor: destination = RHIBlendFactor::DestinationColor; return true;
            case Source::OneMinusDestinationColor: destination = RHIBlendFactor::OneMinusDestinationColor; return true;
            case Source::SourceAlpha: destination = RHIBlendFactor::SourceAlpha; return true;
            case Source::OneMinusSourceAlpha: destination = RHIBlendFactor::OneMinusSourceAlpha; return true;
            case Source::DestinationAlpha: destination = RHIBlendFactor::DestinationAlpha; return true;
            case Source::OneMinusDestinationAlpha: destination = RHIBlendFactor::OneMinusDestinationAlpha; return true;
            case Source::ConstantColor: destination = RHIBlendFactor::ConstantColor; return true;
            case Source::OneMinusConstantColor: destination = RHIBlendFactor::OneMinusConstantColor; return true;
            case Source::SourceAlphaSaturate: destination = RHIBlendFactor::SourceAlphaSaturate; return true;
            }
            return false;
        }

        bool convert_blend_operation(
            shader::ShaderGraphicsPassState::BlendOperation source,
            RHIBlendOperation& destination)
        {
            using Source = shader::ShaderGraphicsPassState::BlendOperation;
            switch (source)
            {
            case Source::Add: destination = RHIBlendOperation::Add; return true;
            case Source::Subtract: destination = RHIBlendOperation::Subtract; return true;
            case Source::ReverseSubtract: destination = RHIBlendOperation::ReverseSubtract; return true;
            case Source::Minimum: destination = RHIBlendOperation::Min; return true;
            case Source::Maximum: destination = RHIBlendOperation::Max; return true;
            }
            return false;
        }

        bool convert_color_write_mask(
            shader::ShaderGraphicsPassState::ColorWriteMask source,
            RHIColorWriteMask& destination)
        {
            using Source = shader::ShaderGraphicsPassState::ColorWriteMask;
            switch (source)
            {
            case Source::None: destination = RHIColorWriteMask::None; return true;
            case Source::Red: destination = RHIColorWriteMask::Red; return true;
            case Source::Green: destination = RHIColorWriteMask::Green; return true;
            case Source::Blue: destination = RHIColorWriteMask::Blue; return true;
            case Source::Alpha: destination = RHIColorWriteMask::Alpha; return true;
            case Source::RedGreen: destination = RHIColorWriteMask::Red | RHIColorWriteMask::Green; return true;
            case Source::RedGreenBlue:
                destination = RHIColorWriteMask::Red | RHIColorWriteMask::Green | RHIColorWriteMask::Blue;
                return true;
            case Source::All: destination = RHIColorWriteMask::All; return true;
            }
            return false;
        }

        bool convert_stencil_face(
            const shader::ShaderGraphicsPassState::StencilFaceState& source,
            RHIGraphicsPipelineDesc::StencilFaceState& destination,
            const char*& invalid_field)
        {
            if (!convert_compare_operation(source.compare_operation, destination.compare_operation))
            {
                invalid_field = "stencil compare operation";
                return false;
            }
            if (!convert_stencil_operation(source.fail_operation, destination.fail_operation))
            {
                invalid_field = "stencil fail operation";
                return false;
            }
            if (!convert_stencil_operation(source.depth_fail_operation, destination.depth_fail_operation))
            {
                invalid_field = "stencil depth-fail operation";
                return false;
            }
            if (!convert_stencil_operation(source.pass_operation, destination.pass_operation))
            {
                invalid_field = "stencil pass operation";
                return false;
            }
            return true;
        }
    }

    RHIResult<RHIGraphicsPipelineDesc> build_shader_graphics_pipeline_desc(
        const RHIGraphicsPipelineDesc& base_desc,
        const shader::ShaderGraphicsPassState& shader_state)
    {
        if (base_desc.color_attachment_count > base_desc.color_blend_attachments.size())
        {
            return invalid_state(base_desc, "active color attachment count");
        }

        RHIGraphicsPipelineDesc candidate = base_desc;
        if (!convert_primitive_topology(shader_state.primitive_topology, candidate.primitive_topology))
        {
            return invalid_state(base_desc, "primitive topology");
        }
        if (!convert_fill_mode(shader_state.fill_mode, candidate.rasterization.polygon_mode))
        {
            return invalid_state(base_desc, "fill mode");
        }
        if (!convert_cull_mode(shader_state.cull_mode, candidate.rasterization.cull_mode))
        {
            return invalid_state(base_desc, "cull mode");
        }
        if (!convert_front_face(shader_state.front_face, candidate.rasterization.front_face))
        {
            return invalid_state(base_desc, "front face");
        }

        candidate.depth_stencil.depth_test_enable = shader_state.depth_test_enable;
        candidate.depth_stencil.depth_write_enable = shader_state.depth_write_enable;
        if (!convert_compare_operation(
                shader_state.depth_compare_operation,
                candidate.depth_stencil.depth_compare_operation))
        {
            return invalid_state(base_desc, "depth compare operation");
        }

        using StencilMode = shader::ShaderGraphicsPassState::StencilMode;
        switch (shader_state.stencil.mode)
        {
        case StencilMode::Off:
        case StencilMode::FrontAndBack:
        case StencilMode::SeparateFaces:
            break;
        default:
            return invalid_state(base_desc, "stencil mode");
        }
        candidate.depth_stencil.stencil_test_enable =
            shader_state.stencil.mode != StencilMode::Off;
        candidate.depth_stencil.stencil_read_mask = shader_state.stencil.read_mask;
        candidate.depth_stencil.stencil_write_mask = shader_state.stencil.write_mask;
        const char* invalid_field = nullptr;
        if (!convert_stencil_face(
                shader_state.stencil.front,
                candidate.depth_stencil.front_face,
                invalid_field) ||
            !convert_stencil_face(
                shader_state.stencil.back,
                candidate.depth_stencil.back_face,
                invalid_field))
        {
            return invalid_state(base_desc, invalid_field);
        }

        RHIGraphicsPipelineDesc::ColorBlendAttachmentState blend;
        blend.blend_enable = shader_state.blend.enabled;
        if (!convert_blend_factor(shader_state.blend.source_color_factor, blend.source_color_factor) ||
            !convert_blend_factor(shader_state.blend.destination_color_factor, blend.destination_color_factor) ||
            !convert_blend_operation(shader_state.blend.color_operation, blend.color_operation) ||
            !convert_blend_factor(shader_state.blend.source_alpha_factor, blend.source_alpha_factor) ||
            !convert_blend_factor(shader_state.blend.destination_alpha_factor, blend.destination_alpha_factor) ||
            !convert_blend_operation(shader_state.blend.alpha_operation, blend.alpha_operation) ||
            !convert_color_write_mask(shader_state.color_write_mask, blend.color_write_mask))
        {
            return invalid_state(base_desc, "blend or color write state");
        }
        for (std::uint32_t index = 0; index < candidate.color_attachment_count; ++index)
        {
            candidate.color_blend_attachments[index] = blend;
        }

        return RHIResult<RHIGraphicsPipelineDesc>::success(std::move(candidate));
    }
}
