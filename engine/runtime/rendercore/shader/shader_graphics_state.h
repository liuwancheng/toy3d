#pragma once

#include "drivers/rhi/rhi.h"
#include "shader/shader_format_types.h"

namespace toy3d
{
    // Builds a new pipeline candidate. Shader-owned state is translated here;
    // pass attachment compatibility, vertex input, shaders and layout remain
    // exactly as supplied by the caller in base_desc.
    RHIResult<RHIGraphicsPipelineDesc> build_shader_graphics_pipeline_desc(
        const RHIGraphicsPipelineDesc& base_desc, const shader::ShaderGraphicsPassState& shader_state);
} // namespace toy3d
