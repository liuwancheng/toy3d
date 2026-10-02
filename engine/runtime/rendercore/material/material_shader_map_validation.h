#pragma once

#include "drivers/rhi/rhi_result.h"
#include "rendercore/shader/shader_map.h"

#include <atomic>
#include <memory>

namespace toy3d
{
    // RT writes status once, then releases complete. GT reads status only after
    // acquire; this is owned completion data, never access to Renderer or RHI.
    struct MaterialShaderMapValidation
    {
        ShaderMapCollectionRef shader_map;
        RHIStatus status;
        std::atomic<bool> complete{false};
    };
    using MaterialShaderMapValidationRef = std::shared_ptr<MaterialShaderMapValidation>;
} // namespace toy3d
