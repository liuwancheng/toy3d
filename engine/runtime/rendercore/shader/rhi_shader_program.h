#pragma once

#include "drivers/rhi/rhi.h"
#include "rendercore/shader/shader_map.h"

#include <optional>

namespace toy3d
{
    struct RHIShaderProgramDesc
    {
        // optional expresses which stages belong to this graphics or compute
        // program without manufacturing empty RHIShaderDesc values.
        std::optional<RHIShaderDesc> vertex_shader;
        std::optional<RHIShaderDesc> pixel_shader;
        std::optional<RHIShaderDesc> compute_shader;
        RHIBindingLayoutDesc binding_layout;
    };

    struct RHIShaderProgram
    {
        RHIShaderRef vertex_shader;
        RHIShaderRef pixel_shader;
        RHIShaderRef compute_shader;
        RHIBindingLayoutRef binding_layout;
    };

    RHIResult<RHIShaderProgramDesc> build_rhi_shader_program_desc(const ShaderMapProgram& program);
} // namespace toy3d
