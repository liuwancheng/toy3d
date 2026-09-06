#pragma once

#include <memory>

namespace toy3d
{
    class PrimitiveSceneProxy;

    // RenderScene-owned registration node. It uniquely owns the Proxy and has no
    // Game-side observer other than the Proxy's opaque address.
    class PrimitiveSceneInfo final
    {
      public:
        explicit PrimitiveSceneInfo(std::unique_ptr<PrimitiveSceneProxy> proxy);
        ~PrimitiveSceneInfo();

        PrimitiveSceneInfo(const PrimitiveSceneInfo&) = delete;
        PrimitiveSceneInfo& operator=(const PrimitiveSceneInfo&) = delete;

        PrimitiveSceneProxy* proxy() const { return proxy_.get(); }

      private:
        std::unique_ptr<PrimitiveSceneProxy> proxy_;
    };
} // namespace toy3d
