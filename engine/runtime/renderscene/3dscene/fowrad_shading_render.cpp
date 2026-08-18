#include "forward_shading_render.h"

namespace toy3d
{
    ForwardSceneRendering::ForwardSceneRendering(
        RHIDevice& device,
        ShaderBytecodeProvider& shader_bytecode_provider)
        : SceneRendering(device, shader_bytecode_provider)
    {

    }

    RHIResult<RHICommandListRef> ForwardSceneRendering::render(RHIFrameContext& frame)
    {
        return SceneRendering::render(frame);
    }

    void ForwardSceneRendering::allocate_scene_rt()
    {

    }

    void ForwardSceneRendering::base_pass()
    {

    }
}
