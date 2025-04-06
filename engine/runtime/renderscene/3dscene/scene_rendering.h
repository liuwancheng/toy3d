#pragma once

namespace toy3d
{
// 这个做基类，会派生出ForwardSceneRendering、DeferredSceneRendering
class SceneRendering
{
public:
    SceneRendering();
    virtual ~SceneRendering();
public:
    virtual void render();
protected:
    void init_views();

    void frustum_cull();

    virtual void allocate_scene_rt();
protected:
    virtual void render_test_pass();
};

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
