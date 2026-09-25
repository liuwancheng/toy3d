#include "application/application.h"

#include "gamescene/world/world.h"

#include <cassert>

namespace toy3d
{
    World& Application::world()
    {
        assert(world_ != nullptr);
        return *world_;
    }

    const World& Application::world() const
    {
        assert(world_ != nullptr);
        return *world_;
    }

    IWindow& Application::window()
    {
        assert(window_ != nullptr);
        return *window_;
    }

    const IWindow& Application::window() const
    {
        assert(window_ != nullptr);
        return *window_;
    }

    bool Application::initialize(World& world, IWindow& window)
    {
        if (world_ != nullptr || window_ != nullptr)
        {
            return false;
        }
        world_ = &world;
        window_ = &window;
        return on_initialize();
    }

    void Application::tick(double delta_time)
    {
        on_tick(delta_time);
    }

    void Application::build_ui()
    {
        on_build_ui();
    }

    bool Application::scene_viewport_extent(Extent& extent) const
    {
        return on_scene_viewport_extent(extent);
    }

    void Application::build_scene_views(std::vector<SceneView>& views, const Extent& extent) const
    {
        on_build_scene_views(views, extent);
    }

    void Application::shutdown()
    {
        if (world_ == nullptr || window_ == nullptr)
        {
            return;
        }
        on_shutdown();
        window_ = nullptr;
        world_ = nullptr;
    }
} // namespace toy3d
