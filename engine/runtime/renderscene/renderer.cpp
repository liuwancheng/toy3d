#include "renderscene/renderer.h"

#include <cassert>
#include <memory>
#include <utility>

#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "renderscene/render_scene.h"
#include "renderscene/view/scene_renderer.h"
#include "task_graph/task_graph_interface.h"

namespace toy3d
{
    Renderer::Renderer(TaskGraphInterface& task_graph)
        : task_graph_(task_graph)
    {
    }

    Renderer::~Renderer()
    {
        // Engine must run logical-RT teardown before destroying the stable GT shell;
        // otherwise RenderScene would be destroyed on the wrong thread.
        assert(!render_scene_);
    }

    ThreadStatus Renderer::initialize()
    {
        if (!is_on_logical_rendering_thread())
        {
            return ThreadStatus::failure(
                ThreadErrorCode::InvalidCaller,
                "Renderer must initialize on the logical Rendering Thread");
        }
        if (render_scene_)
        {
            return ThreadStatus::failure(
                ThreadErrorCode::InvalidState,
                "Renderer is already initialized");
        }

        render_scene_ = std::make_unique<RenderScene>(task_graph_);
        return ThreadStatus::success();
    }

    ThreadStatus Renderer::teardown()
    {
        if (!is_on_logical_rendering_thread())
        {
            return ThreadStatus::failure(
                ThreadErrorCode::InvalidCaller,
                "Renderer must teardown on the logical Rendering Thread");
        }
        if (!render_scene_)
        {
            return ThreadStatus::failure(
                ThreadErrorCode::InvalidState,
                "Renderer is not initialized");
        }

        render_scene_.reset();
        return ThreadStatus::success();
    }

    void Renderer::draw_scene(
        std::unique_ptr<SceneRenderer> scene_renderer)
    {
        enqueue_render_command(
            "DrawScene",
            [this, scene_renderer = std::move(scene_renderer)]() mutable noexcept
            {
                if (!render_scene_ || !scene_renderer)
                {
                    TOY_LOG_ERROR(
                        "Renderer Draw requires initialized scene state and a SceneRenderer.");
                    return;
                }
                scene_renderer->render(*render_scene_);
                scene_renderer.reset();
            });
    }

    SceneInterface* Renderer::scene_interface() const
    {
        return render_scene_.get();
    }

    bool Renderer::is_on_logical_rendering_thread() const
    {
        const NamedThread current_thread = task_graph_.get_current_thread_if_known();
        return current_thread != NamedThread::Unknown
            && current_thread == task_graph_.get_render_thread();
    }
}
