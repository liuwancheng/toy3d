#pragma once

#include "application/application.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/material/material.h"
#include "rendercore/texture/texture.h"

namespace toy3d
{
    class Actor;
    class World;
} // namespace toy3d

class CubeApplication final : public toy3d::Application
{
  public:
    CubeApplication(bool automated_window_events, bool auto_close);

    bool setup_failed() const { return setup_failed_; }

  protected:
    bool on_initialize() override;
    void on_tick(double delta_time) override;
    void on_build_ui() override;
    void on_build_scene_views(std::vector<toy3d::SceneView>& views, const toy3d::Extent& extent) const override;
    void on_shutdown() override;

  private:
    bool release_scene_resources();

    toy3d::TextureRef warm_tint_texture_;
    toy3d::TextureRef cool_tint_texture_;
    toy3d::MaterialInstanceRef material_instance_;
    toy3d::StaticMeshRef mesh_;
    toy3d::Actor* actor_ = nullptr;
    float camera_x_ = 0.0f;
    bool animate_camera_ = true;
    bool animate_material_ = true;
    bool show_diagnostics_ = false;
    bool setup_failed_ = false;
    bool actor_destroyed_ = false;
    bool material_released_ = false;
    bool resources_released_ = false;
    bool automated_window_events_ = false;
    bool auto_close_ = false;
};
