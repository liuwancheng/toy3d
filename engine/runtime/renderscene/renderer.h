#pragma once

#include "threading/threading_types.h"

#include <memory>

namespace toy3d
{
    class RenderScene;
    class SceneRenderer;
    class SceneInterface;
    class TaskGraphInterface;

    // Engine-owned stable shell. Its mutable Render-side domain is initialized and
    // torn down only on the logical Rendering Thread.
    class Renderer final
    {
    public:
        explicit Renderer(TaskGraphInterface& task_graph);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) = delete;
        Renderer& operator=(Renderer&&) = delete;

        ThreadStatus initialize();
        ThreadStatus teardown();
        void draw_scene(std::unique_ptr<SceneRenderer> scene_renderer);
        // Published only between successful logical-RT initialize and teardown.
        // The pointer is non-owning and exposes no concrete RenderScene state to GT.
        SceneInterface* scene_interface() const;

    private:
        bool is_on_logical_rendering_thread() const;

        TaskGraphInterface& task_graph_;
        std::unique_ptr<RenderScene> render_scene_;
    };
}
