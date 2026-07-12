#include "renderscene/3dscene/scene_render.h"

namespace toy3d
{
    void SceneRendering::render_test_pass()
    {
        // The legacy global-RHI pass was removed. This pass will be connected
        // through the renderer-owned RHIDevice and graphics command context.
    }
}
