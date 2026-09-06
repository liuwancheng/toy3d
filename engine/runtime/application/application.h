#pragma once

#include "platform/window_interface.h"
#include "rendercore/view/scene_view.h"

#include <vector>

namespace toy3d
{
    class Engine;
    class World;

    // Application owns project-level policy and state. Engine owns the World
    // and Window and binds them once before World initialization; gameplay
    // updates remain in Actor/Component tick rather than a World-wide callback.
    class Application
    {
      public:
        virtual ~Application() = default;

      protected:
        World& world();
        const World& world() const;
        IWindow& window();
        const IWindow& window() const;

        virtual bool on_initialize() = 0;
        virtual void on_tick(double) {}
        // Called only while the GT Dear ImGui frame is active. Applications
        // may build widgets through ImGui core but receive no renderer service.
        virtual void on_build_ui() {}
        virtual void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const = 0;
        virtual void on_shutdown() {}

      private:
        friend class Engine;

        bool initialize(World& world, IWindow& window);
        void tick(double delta_time);
        void build_ui();
        void build_scene_views(std::vector<SceneView>& views, const Extent& extent) const;
        void shutdown();

        // Engine owns both observers and keeps them alive for the complete
        // initialize/tick/view/shutdown interval.
        World* world_ = nullptr;
        IWindow* window_ = nullptr;
    };
} // namespace toy3d
