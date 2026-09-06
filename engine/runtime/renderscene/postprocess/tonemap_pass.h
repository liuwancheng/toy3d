#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/global_shader_type.h"

#include <cstdint>

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class ShaderMapProgram;

    struct TonemapParameters
    {
        float exposure_ev = 0.0F;
    };

    // CPU reference for deterministic numeric tests and future golden-image
    // tooling. Runtime pixels are produced by the matching shader path.
    float tonemap_sdr_channel_reference(float linear_value, float exposure_ev);
    const GlobalShaderType& tonemap_global_shader_type();

    struct TonemapPassTarget
    {
        RHITextureViewRef color_view;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        PixelFormat format = PixelFormat::B8G8R8A8UNorm;
        std::uint32_t sample_count = 1u;
    };

    class TonemapPassResources final
    {
    public:
        TonemapPassResources() = default;
        ~TonemapPassResources() = default;

        TonemapPassResources(const TonemapPassResources&) = delete;
        TonemapPassResources& operator=(const TonemapPassResources&) = delete;

        RHIStatus initialize(
            RHIDevice& device,
            RHIShaderProgramCache& shader_program_cache,
            const GlobalShaderMap& global_shader_map);
        void release() noexcept;

        RHIStatus render(
            RHIDevice& device,
            RHIGraphicsCommandContext& context,
            const RHITextureViewRef& scene_color,
            const TonemapPassTarget& target,
            const TonemapParameters& parameters) const;

        bool initialized() const noexcept;

    private:
        const ShaderMapBinding* constant_buffer_binding_ = nullptr;
        const ShaderMapBinding::ConstantMember* exposure_binding_ = nullptr;
        const ShaderMapBinding* scene_color_binding_ = nullptr;
        const ShaderMapBinding* scene_sampler_binding_ = nullptr;
        const ShaderMapProgram* shader_program_ = nullptr;
        RHIShaderProgramRef rhi_program_;
        RHISamplerRef sampler_;
        RHIGraphicsPipelineRef pipeline_;
    };
}
