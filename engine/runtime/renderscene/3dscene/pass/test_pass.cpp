#include "renderscene/3dscene/scene_rendering.h"
#include "renderscene/3dscene/scene_render_target.h"
#include "rhi/rhi.h"

namespace toy3d
{
    void SceneRendering::render_test_pass()
    {
        // 从全局scene_render_target_mgr中获取color buffer & depth buffer
        // 1、begin pass提供pass_info
        // 2、设置view port信息
        // 3、dispatch draw
        // 4、end pass
        SceneRenderTargetMgr* rt_mgr = SceneRenderTargetMgr::Get();
        rt_mgr->allocate();
        // init views

        RHIRenderPassInfo test_pass_info(
            rt_mgr->get_scene_color(),
            ERenderTargetLoadAction::EClear,
            ERenderTargetStoreAction::EStore,
            rt_mgr->get_scene_depth(),
            ERenderTargetLoadAction::EClear,
            ERenderTargetStoreAction::EStore
        );
        g_rhi->begin_render_pass(test_pass_info, "RenderTrianglePass");


        // 可以用来处理Resolve RT
        g_rhi->end_render_pass();
    }
}