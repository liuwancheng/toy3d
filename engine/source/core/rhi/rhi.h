#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include "rhi_resource.h"

namespace toy3d
{

    struct VertexShader
    {

    };

    struct FragmentShader
    {

    };

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
        virtual VertexShader create_vertex_shader(std::string code) = 0;

        virtual FragmentShader create_fragment_shader(std::string code) = 0;

        virtual RHITextureRef create_texture2d(uint32_t x, uint32_t y, uint8_t format, uint32_t samples, uint32_t flags) = 0;

        virtual void begin_render_pass(const RHIRenderPassInfo &info, std::string pass_name) = 0;

        virtual void end_render_pass() = 0;
    };

    extern IDynamicRHI* g_rhi;
}