#pragma once

#include "drivers/rhi/rhi_device.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace toy3d
{
    struct RenderTargetDesc
    {
        bool operator<(const RenderTargetDesc& other) const;

        RHIFormat format = RHIFormat::Unknown;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t array_layers = 1;
        std::uint32_t sample_count = 1;
        RHIResourceUsage usage = RHIResourceUsage::None;
        RHIClearValue clear_value;
        std::string debug_name;
    };

    class SceneRenderTargetMgr
    {
    public:
        static SceneRenderTargetMgr& get();

        void set_buffer_size(std::uint32_t width, std::uint32_t height);
        RHIStatus allocate(RHIDevice& device);

        const RHITextureRef& scene_color() const;
        const RHITextureRef& scene_depth() const;

    private:
        RHIResult<RHITextureRef> find_or_create(
            RHIDevice& device,
            const RenderTargetDesc& desc);

        std::uint32_t buffer_width = 0;
        std::uint32_t buffer_height = 0;
        RHITextureRef scene_color_texture;
        RHITextureRef scene_depth_texture;
        std::map<RenderTargetDesc, RHITextureRef> render_target_pool;
    };
}
