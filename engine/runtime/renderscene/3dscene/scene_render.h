#pragma once

#include "drivers/rhi/rhi.h"

namespace toy3d
{
// 这个做基类，会派生出ForwardSceneRendering、DeferredSceneRendering
class SceneRendering
{
public:
    explicit SceneRendering(RHIDevice& device);
    virtual ~SceneRendering() = default;

    virtual RHIResult<RHICommandListRef> render(RHIFrameContext& frame);
protected:
    void init_views();

    void frustum_cull();

    virtual void allocate_scene_rt();
protected:
    RHIStatus initialize_test_pass_resources();
    RHIStatus prepare_test_pass_texture(RHIGraphicsCommandContext& context);
    virtual RHIStatus render_test_pass(
        RHIGraphicsCommandContext& context,
        const RHIFrameContext& frame);

    RHIDevice& rhi_device;
    RHITextureRef test_texture;
    RHITextureViewRef test_texture_view;
    RHISamplerRef test_sampler;
    RHIBindingLayoutRef test_binding_layout;
    RHIBindingSetRef test_binding_set;
    RHIGraphicsPipelineRef test_pipeline;
    bool test_texture_uploaded = false;
};

} // namespace toy3d
