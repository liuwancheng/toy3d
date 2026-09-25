#pragma once

#include "application/application.h"
#include "rendercore/material/material.h"

namespace toy3d
{
    class StaticMeshActor;

    class EditorApplication final : public Application
    {
      protected:
        bool on_initialize() override;
        void on_shutdown() override;
        void on_build_ui() override;
        bool on_scene_viewport_extent(Extent& extent) const override;
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override;

      private:
        Extent scene_extent_;
        StaticMeshActor* preview_actor_ = nullptr;
        MaterialInstanceRef preview_material_;
    };
} // namespace toy3d
