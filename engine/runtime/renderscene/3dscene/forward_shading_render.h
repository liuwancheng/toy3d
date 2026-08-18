#pragma once
#include "scene_render.h"

namespace toy3d
{

class ForwardSceneRendering : public SceneRendering
{
public:
    ForwardSceneRendering(RHIDevice& device, ShaderBytecodeProvider& shader_bytecode_provider);
    ~ForwardSceneRendering() override = default;

    RHIResult<RHICommandListRef> render(RHIFrameContext& frame) override;

private:

    virtual void allocate_scene_rt() override;

    void base_pass();
};

} // namespace toy3d
