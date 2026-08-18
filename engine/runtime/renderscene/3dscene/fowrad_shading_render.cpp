#include "forward_shading_render.h"

namespace toy3d
{
    ForwardSceneRendering::ForwardSceneRendering(
        RHIDevice& device,
        ShaderMap& shader_map)
        : SceneRendering(device, shader_map)
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
