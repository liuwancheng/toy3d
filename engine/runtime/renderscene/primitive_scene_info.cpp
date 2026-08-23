#include "renderscene/primitive_scene_info.h"

#include <cassert>
#include <utility>

#include "rendercore/scene/primitive_scene_proxy.h"

namespace toy3d
{
    PrimitiveSceneInfo::PrimitiveSceneInfo(
        std::unique_ptr<PrimitiveSceneProxy> proxy)
        : proxy_(std::move(proxy))
    {
        assert(proxy_ != nullptr);
    }

    PrimitiveSceneInfo::~PrimitiveSceneInfo() = default;
}
