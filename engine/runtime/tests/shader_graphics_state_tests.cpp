#include "rendercore/shader/shader_graphics_state.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::RHIGraphicsPipelineDesc make_base_desc()
    {
        toy3d::RHIGraphicsPipelineDesc desc;
        desc.color_attachment_count = 2;
        desc.color_formats[0] = toy3d::PixelFormat::R8G8B8A8UNorm;
        desc.color_formats[1] = toy3d::PixelFormat::R16G16B16A16Float;
        desc.depth_stencil_format = toy3d::PixelFormat::D32Float;
        desc.sample_count = 4;
        desc.rasterization.depth_clamp_enable = true;
        desc.color_blend_attachments[2].blend_enable = true;
        desc.debug_name = "Toy3d/Test/Graphics:Forward";
        return desc;
    }

    template<typename Source, typename Destination, std::size_t Size, typename SetSource, typename ReadDestination>
    void check_mapping(
        const std::array<Source, Size>& source_values,
        const std::array<Destination, Size>& expected_values,
        SetSource set_source,
        ReadDestination read_destination,
        const char* message)
    {
        for (std::size_t index = 0; index < Size; ++index)
        {
            toy3d::shader::ShaderGraphicsPassState state;
            set_source(state, source_values[index]);
            auto converted = toy3d::build_shader_graphics_pipeline_desc(
                make_base_desc(), state);
            check(converted.succeeded(), message);
            if (converted.succeeded())
            {
                check(read_destination(converted.value()) == expected_values[index], message);
            }
        }
    }
}

int main()
{
    using State = toy3d::shader::ShaderGraphicsPassState;

    check_mapping(
        std::array<State::PrimitiveTopology, 5>{
            State::PrimitiveTopology::PointList,
            State::PrimitiveTopology::LineList,
            State::PrimitiveTopology::LineStrip,
            State::PrimitiveTopology::TriangleList,
            State::PrimitiveTopology::TriangleStrip},
        std::array<toy3d::RHIPrimitiveTopology, 5>{
            toy3d::RHIPrimitiveTopology::PointList,
            toy3d::RHIPrimitiveTopology::LineList,
            toy3d::RHIPrimitiveTopology::LineStrip,
            toy3d::RHIPrimitiveTopology::TriangleList,
            toy3d::RHIPrimitiveTopology::TriangleStrip},
        [](State& state, State::PrimitiveTopology value) { state.primitive_topology = value; },
        [](const toy3d::RHIGraphicsPipelineDesc& desc) { return desc.primitive_topology; },
        "all primitive topologies must map exactly");

    check_mapping(
        std::array<State::CullMode, 3>{State::CullMode::None, State::CullMode::Front, State::CullMode::Back},
        std::array<toy3d::RHICullMode, 3>{toy3d::RHICullMode::None, toy3d::RHICullMode::Front, toy3d::RHICullMode::Back},
        [](State& state, State::CullMode value) { state.cull_mode = value; },
        [](const toy3d::RHIGraphicsPipelineDesc& desc) { return desc.rasterization.cull_mode; },
        "all cull modes must map exactly");

    check_mapping(
        std::array<State::FrontFace, 2>{State::FrontFace::Clockwise, State::FrontFace::CounterClockwise},
        std::array<toy3d::RHIFrontFace, 2>{toy3d::RHIFrontFace::Clockwise, toy3d::RHIFrontFace::CounterClockwise},
        [](State& state, State::FrontFace value) { state.front_face = value; },
        [](const toy3d::RHIGraphicsPipelineDesc& desc) { return desc.rasterization.front_face; },
        "all front-face modes must map exactly");

    check_mapping(
        std::array<State::FillMode, 2>{State::FillMode::Solid, State::FillMode::Wireframe},
        std::array<toy3d::RHIPolygonMode, 2>{toy3d::RHIPolygonMode::Fill, toy3d::RHIPolygonMode::Line},
        [](State& state, State::FillMode value) { state.fill_mode = value; },
        [](const toy3d::RHIGraphicsPipelineDesc& desc) { return desc.rasterization.polygon_mode; },
        "all fill modes must map exactly");

    const std::array<State::CompareOperation, 8> compare_sources{
        State::CompareOperation::Never, State::CompareOperation::Less,
        State::CompareOperation::Equal, State::CompareOperation::LessEqual,
        State::CompareOperation::Greater, State::CompareOperation::NotEqual,
        State::CompareOperation::GreaterEqual, State::CompareOperation::Always};
    const std::array<toy3d::RHICompareOperation, 8> compare_destinations{
        toy3d::RHICompareOperation::Never, toy3d::RHICompareOperation::Less,
        toy3d::RHICompareOperation::Equal, toy3d::RHICompareOperation::LessEqual,
        toy3d::RHICompareOperation::Greater, toy3d::RHICompareOperation::NotEqual,
        toy3d::RHICompareOperation::GreaterEqual, toy3d::RHICompareOperation::Always};
    check_mapping(
        compare_sources, compare_destinations,
        [](State& state, State::CompareOperation value) { state.depth_compare_operation = value; },
        [](const toy3d::RHIGraphicsPipelineDesc& desc) { return desc.depth_stencil.depth_compare_operation; },
        "all compare operations must map exactly");

    const std::array<State::StencilOperation, 8> stencil_sources{
        State::StencilOperation::Keep, State::StencilOperation::Zero,
        State::StencilOperation::Replace, State::StencilOperation::IncrementClamp,
        State::StencilOperation::DecrementClamp, State::StencilOperation::Invert,
        State::StencilOperation::IncrementWrap, State::StencilOperation::DecrementWrap};
    const std::array<toy3d::RHIStencilOperation, 8> stencil_destinations{
        toy3d::RHIStencilOperation::Keep, toy3d::RHIStencilOperation::Zero,
        toy3d::RHIStencilOperation::Replace, toy3d::RHIStencilOperation::IncrementClamp,
        toy3d::RHIStencilOperation::DecrementClamp, toy3d::RHIStencilOperation::Invert,
        toy3d::RHIStencilOperation::IncrementWrap, toy3d::RHIStencilOperation::DecrementWrap};
    check_mapping(
        stencil_sources, stencil_destinations,
        [](State& state, State::StencilOperation value) {
            state.stencil.mode = State::StencilMode::SeparateFaces;
            state.stencil.front.pass_operation = value;
        },
        [](const toy3d::RHIGraphicsPipelineDesc& desc) { return desc.depth_stencil.front_face.pass_operation; },
        "all stencil operations must map exactly");

    const std::array<State::BlendFactor, 13> blend_factor_sources{
        State::BlendFactor::Zero, State::BlendFactor::One,
        State::BlendFactor::SourceColor, State::BlendFactor::OneMinusSourceColor,
        State::BlendFactor::DestinationColor, State::BlendFactor::OneMinusDestinationColor,
        State::BlendFactor::SourceAlpha, State::BlendFactor::OneMinusSourceAlpha,
        State::BlendFactor::DestinationAlpha, State::BlendFactor::OneMinusDestinationAlpha,
        State::BlendFactor::ConstantColor, State::BlendFactor::OneMinusConstantColor,
        State::BlendFactor::SourceAlphaSaturate};
    const std::array<toy3d::RHIBlendFactor, 13> blend_factor_destinations{
        toy3d::RHIBlendFactor::Zero, toy3d::RHIBlendFactor::One,
        toy3d::RHIBlendFactor::SourceColor, toy3d::RHIBlendFactor::OneMinusSourceColor,
        toy3d::RHIBlendFactor::DestinationColor, toy3d::RHIBlendFactor::OneMinusDestinationColor,
        toy3d::RHIBlendFactor::SourceAlpha, toy3d::RHIBlendFactor::OneMinusSourceAlpha,
        toy3d::RHIBlendFactor::DestinationAlpha, toy3d::RHIBlendFactor::OneMinusDestinationAlpha,
        toy3d::RHIBlendFactor::ConstantColor, toy3d::RHIBlendFactor::OneMinusConstantColor,
        toy3d::RHIBlendFactor::SourceAlphaSaturate};
    check_mapping(
        blend_factor_sources, blend_factor_destinations,
        [](State& state, State::BlendFactor value) { state.blend.source_color_factor = value; },
        [](const toy3d::RHIGraphicsPipelineDesc& desc) { return desc.color_blend_attachments[0].source_color_factor; },
        "all blend factors must map exactly");

    check_mapping(
        std::array<State::BlendOperation, 5>{
            State::BlendOperation::Add, State::BlendOperation::Subtract,
            State::BlendOperation::ReverseSubtract, State::BlendOperation::Minimum,
            State::BlendOperation::Maximum},
        std::array<toy3d::RHIBlendOperation, 5>{
            toy3d::RHIBlendOperation::Add, toy3d::RHIBlendOperation::Subtract,
            toy3d::RHIBlendOperation::ReverseSubtract, toy3d::RHIBlendOperation::Min,
            toy3d::RHIBlendOperation::Max},
        [](State& state, State::BlendOperation value) { state.blend.color_operation = value; },
        [](const toy3d::RHIGraphicsPipelineDesc& desc) { return desc.color_blend_attachments[0].color_operation; },
        "all blend operations must map exactly");

    check_mapping(
        std::array<State::ColorWriteMask, 8>{
            State::ColorWriteMask::None, State::ColorWriteMask::Red,
            State::ColorWriteMask::Green, State::ColorWriteMask::Blue,
            State::ColorWriteMask::Alpha, State::ColorWriteMask::RedGreen,
            State::ColorWriteMask::RedGreenBlue, State::ColorWriteMask::All},
        std::array<toy3d::RHIColorWriteMask, 8>{
            toy3d::RHIColorWriteMask::None, toy3d::RHIColorWriteMask::Red,
            toy3d::RHIColorWriteMask::Green, toy3d::RHIColorWriteMask::Blue,
            toy3d::RHIColorWriteMask::Alpha,
            toy3d::RHIColorWriteMask::Red | toy3d::RHIColorWriteMask::Green,
            toy3d::RHIColorWriteMask::Red | toy3d::RHIColorWriteMask::Green | toy3d::RHIColorWriteMask::Blue,
            toy3d::RHIColorWriteMask::All},
        [](State& state, State::ColorWriteMask value) { state.color_write_mask = value; },
        [](const toy3d::RHIGraphicsPipelineDesc& desc) { return desc.color_blend_attachments[0].color_write_mask; },
        "all color-write masks must map exactly");

    State complete_state;
    complete_state.depth_test_enable = true;
    complete_state.depth_write_enable = true;
    complete_state.depth_compare_operation = State::CompareOperation::GreaterEqual;
    complete_state.stencil.mode = State::StencilMode::FrontAndBack;
    complete_state.stencil.read_mask = 0x3cu;
    complete_state.stencil.write_mask = 0xc3u;
    complete_state.blend.enabled = true;
    auto complete = toy3d::build_shader_graphics_pipeline_desc(make_base_desc(), complete_state);
    check(complete.succeeded(), "complete reversed-Z state must convert");
    if (complete.succeeded())
    {
        const auto& desc = complete.value();
        check(desc.depth_stencil.depth_test_enable && desc.depth_stencil.depth_write_enable &&
              desc.depth_stencil.depth_compare_operation == toy3d::RHICompareOperation::GreaterEqual,
            "reversed-Z depth state must be preserved");
        check(desc.depth_stencil.stencil_test_enable &&
              desc.depth_stencil.stencil_read_mask == 0x3cu &&
              desc.depth_stencil.stencil_write_mask == 0xc3u,
            "stencil mode and masks must be preserved");
        check(desc.color_blend_attachments[0].blend_enable &&
              desc.color_blend_attachments[1].blend_enable &&
              desc.color_blend_attachments[2].blend_enable,
            "only active color attachment blend state must be overwritten");
        check(desc.color_formats[0] == toy3d::PixelFormat::R8G8B8A8UNorm &&
              desc.color_formats[1] == toy3d::PixelFormat::R16G16B16A16Float &&
              desc.depth_stencil_format == toy3d::PixelFormat::D32Float &&
              desc.sample_count == 4 && desc.rasterization.depth_clamp_enable,
            "pass-owned compatibility fields must remain unchanged");
    }

    toy3d::RHIGraphicsPipelineDesc base_desc = make_base_desc();
    State invalid;
    invalid.primitive_topology = static_cast<State::PrimitiveTopology>(0xffu);
    auto rejected = toy3d::build_shader_graphics_pipeline_desc(base_desc, invalid);
    check(!rejected.succeeded() && rejected.status().code() == toy3d::RHIErrorCode::InvalidArgument,
        "unknown enum values must fail without fallback");
    check(rejected.status().message().find("Toy3d/Test/Graphics:Forward") != std::string::npos &&
          rejected.status().message().find("primitive topology") != std::string::npos,
        "failure must identify the shader pass and field");
    check(base_desc.primitive_topology == toy3d::RHIPrimitiveTopology::TriangleList &&
          base_desc.color_blend_attachments[0].blend_enable == false &&
          base_desc.color_blend_attachments[2].blend_enable,
        "failure must not modify the base descriptor");

    if (failure_count != 0)
    {
        return 1;
    }
    std::cout << "Shader graphics state tests passed\n";
    return 0;
}
