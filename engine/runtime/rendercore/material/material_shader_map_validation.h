#pragma once

#include "drivers/rhi/rhi_result.h"
#include "rendercore/shader/shader_map.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace toy3d
{
    class MaterialRenderProxy;
    struct MaterialShaderMapValidationTarget
    {
        // Opaque GT identity. The owner's existing FIFO release protocol keeps
        // this address valid while the RT validation command consumes it.
        MaterialRenderProxy* proxy = nullptr;
        ShaderMapCollectionRef shader_map;
    };
    struct SceneMaterialUsageRevision
    {
        std::shared_ptr<const std::atomic<std::uint64_t>> generation;
        std::uint64_t value = 0u;
    };
    // RT writes status once, then releases complete. GT reads status only after
    // acquire; this is owned completion data, never access to Renderer or RHI.
    struct MaterialShaderMapValidation
    {
        ShaderMapCollectionRef shader_map;
        std::vector<MaterialShaderMapValidationTarget> targets;
        bool exact_targets = false;
        std::vector<SceneMaterialUsageRevision> scene_revisions;
        RHIStatus status;
        std::atomic<bool> complete{false};
    };
    using MaterialShaderMapValidationRef = std::shared_ptr<MaterialShaderMapValidation>;
} // namespace toy3d
