#pragma once

#include "drivers/rhi/rhi.h"

#include <cstdint>

namespace toy3d
{
class ShaderBytecodeProvider;
// 这个做基类，会派生出ForwardSceneRendering、DeferredSceneRendering
class SceneRendering
{
public:
    SceneRendering(RHIDevice& device, ShaderBytecodeProvider& shader_bytecode_provider);
    virtual ~SceneRendering() = default;

    virtual RHIResult<RHICommandListRef> render(RHIFrameContext& frame);
protected:
    void init_views();

    void frustum_cull();

    virtual void allocate_scene_rt();
protected:
    RHIStatus initialize_test_pass_resources();
    RHIStatus initialize_test_depth_resources(const RHIFrameContext& frame);
    RHIStatus prepare_test_pass_texture(RHIGraphicsCommandContext& context);
    RHIStatus prepare_test_pass_depth(RHIGraphicsCommandContext& context);
    virtual RHIStatus render_test_pass(
        RHIGraphicsCommandContext& context,
        const RHIFrameContext& frame);

    RHIDevice& rhi_device;
    ShaderBytecodeProvider& shader_bytecode_provider;
    RHITextureRef test_texture;
    RHITextureViewRef test_texture_view;
    RHITextureRef test_depth_texture;
    RHITextureViewRef test_depth_view;
    RHISamplerRef test_sampler;
    RHIBindingLayoutRef test_binding_layout;
    RHIBindingSetRef test_binding_set;
    RHIGraphicsPipelineRef test_pipeline;
    bool test_texture_uploaded = false;
    bool test_depth_transitioned = false;
    std::uint32_t test_depth_width = 0;
    std::uint32_t test_depth_height = 0;
};

} // namespace toy3d
