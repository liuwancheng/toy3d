#pragma once

#include "core/misc/pch.h"
#include "rhi_definitions.h"

namespace toy3d
{   

    class IDynamicRHI
    {
    public:
        IDynamicRHI() = default;

        virtual ~IDynamicRHI() {}

        virtual void init() = 0;

        virtual void clear() = 0;

        virtual void begin_frame() = 0;

        virtual void submit() = 0;

        virtual void end_frame() = 0;

        // 所有的图形API抽象层
        virtual RHIVertexShader create_vertex_shader(std::string code) = 0;

        virtual RHIPixelShader create_fragment_shader(std::string code) = 0;

        virtual RHITextureRef create_texture2d(uint32_t x, uint32_t y, uint8_t format, uint32_t samples, uint32_t flags) = 0;

        virtual void begin_render_pass(const RHIRenderPassInfo &info, std::string pass_name) = 0;

        virtual void end_render_pass() = 0;
    };

}// namespace toy3d