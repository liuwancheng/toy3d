#pragma once
#include "scene_render.h"

namespace toy3d
{

class ForwardSceneRendering : public SceneRendering
{
public:
    ForwardSceneRendering();
    ~ForwardSceneRendering();
private:
    virtual void render() override;

    virtual void allocate_scene_rt() override;

    void base_pass();
};

} // namespace toy3d