#include "renderscene/view/scene_renderer.h"

#include <utility>

namespace toy3d
{
    SceneRenderer::SceneRenderer(SceneViewFamily view_family)
        : view_family_(std::move(view_family))
    {}

    SceneRenderer::~SceneRenderer() = default;
}
