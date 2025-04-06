#include "scene_rendering.h"

namespace toy3d
{
    /*****************   SceneRendering  *****************/
    SceneRendering::SceneRendering()
    {

    }

    SceneRendering::~SceneRendering()
    {

    }

    void SceneRendering::init_views()
    {

    }

    void SceneRendering::allocate_scene_rt()
    {
    }

    void SceneRendering::render()
    {
        render_test_pass();
    }
    /*****************   SceneRendering  *****************/
    ForwardSceneRendering::ForwardSceneRendering()
    :SceneRendering()
    {

    }

    ForwardSceneRendering::~ForwardSceneRendering()
    {

    }

    void ForwardSceneRendering::render()
    {
        render_test_pass();
    }

    void ForwardSceneRendering::allocate_scene_rt()
    {

    }

    void ForwardSceneRendering::base_pass()
    {

    }
}