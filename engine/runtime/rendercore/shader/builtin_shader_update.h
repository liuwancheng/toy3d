#pragma once

#include "drivers/rhi/rhi_result.h"
#include "rendercore/shader/shader_map_collection.h"

#include <atomic>
#include <memory>
#include <vector>

namespace toy3d
{
    enum class BuiltinShaderDecision
    {
        Pending,
        Commit,
        Discard
    };

    // Owned GT/RT handshake. Input is immutable after dispatch; RT releases
    // prepared before GT reads status, and resolved before GT reads applied.
    // Prepared GPU resources stay exclusively in Renderer, never in this object.
    struct BuiltinShaderUpdate
    {
        std::vector<ShaderMapCollectionRef> shader_maps;
        RHIStatus status;
        std::atomic<bool> prepared{false};
        std::atomic<BuiltinShaderDecision> decision{BuiltinShaderDecision::Pending};
        bool applied = false;
        std::atomic<bool> resolved{false};
    };
    using BuiltinShaderUpdateRef = std::shared_ptr<BuiltinShaderUpdate>;
} // namespace toy3d
