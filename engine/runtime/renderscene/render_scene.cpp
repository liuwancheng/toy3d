#include "renderscene/render_scene.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "renderscene/primitive_scene_info.h"
#include "task_graph/task_graph_interface.h"

namespace toy3d
{
    RenderScene::RenderScene(TaskGraphInterface& task_graph)
        : task_graph_(task_graph)
    {
        // RenderScene is mutable Render-side state and may only enter its lifetime
        // after the logical Rendering Thread has been attached.
        assert(is_on_logical_rendering_thread());
    }

    RenderScene::~RenderScene()
    {
        // Renderer must release all scene-owned state before the logical RT returns.
        assert(is_on_logical_rendering_thread());
        primitives_.clear();
    }

    void RenderScene::add_primitive(
        std::unique_ptr<PrimitiveSceneProxy> proxy)
    {
        enqueue_render_command(
            "AddPrimitive",
            [this, proxy = std::move(proxy)]() mutable noexcept
            {
                add_primitive_render_thread(std::move(proxy));
            });
    }

    void RenderScene::update_primitive_transform(
        PrimitiveSceneProxy* proxy,
        Matrix4 world_transform,
        AxisAlignedBounds world_bounds,
        bool visible)
    {
        enqueue_render_command(
            "UpdatePrimitiveTransform",
            [this,
             proxy,
             world_transform = std::move(world_transform),
             world_bounds = std::move(world_bounds),
             visible]() mutable noexcept
            {
                update_primitive_transform_render_thread(
                    proxy,
                    std::move(world_transform),
                    std::move(world_bounds),
                    visible);
            });
    }

    void RenderScene::remove_primitive(PrimitiveSceneProxy* proxy)
    {
        enqueue_render_command(
            "RemovePrimitive",
            [this, proxy]() noexcept
            {
                remove_primitive_render_thread(proxy);
            });
    }

    void RenderScene::add_primitive_render_thread(
        std::unique_ptr<PrimitiveSceneProxy> proxy) noexcept
    {
        assert(is_on_logical_rendering_thread());
        if (!proxy)
        {
            TOY_LOG_ERROR("RenderScene cannot add a null PrimitiveSceneProxy.");
            return;
        }

        PrimitiveSceneProxy* const proxy_identity = proxy.get();
        const auto duplicate = std::find_if(
            primitives_.begin(),
            primitives_.end(),
            [proxy_identity](const std::unique_ptr<PrimitiveSceneInfo>& info)
            {
                return info->proxy() == proxy_identity;
            });
        if (duplicate != primitives_.end())
        {
            TOY_LOG_ERROR("RenderScene received a duplicate PrimitiveSceneProxy add.");
            return;
        }

        primitives_.push_back(
            std::make_unique<PrimitiveSceneInfo>(std::move(proxy)));
    }

    void RenderScene::update_primitive_transform_render_thread(
        PrimitiveSceneProxy* proxy,
        Matrix4 world_transform,
        AxisAlignedBounds world_bounds,
        bool visible) noexcept
    {
        assert(is_on_logical_rendering_thread());
        const auto found = std::find_if(
            primitives_.begin(),
            primitives_.end(),
            [proxy](const std::unique_ptr<PrimitiveSceneInfo>& info)
            {
                return info->proxy() == proxy;
            });
        if (found == primitives_.end())
        {
            TOY_LOG_ERROR("RenderScene received an update for an unregistered PrimitiveSceneProxy.");
            return;
        }

        (*found)->proxy()->update_transform(
            std::move(world_transform),
            std::move(world_bounds),
            visible);
    }

    void RenderScene::remove_primitive_render_thread(
        PrimitiveSceneProxy* proxy) noexcept
    {
        assert(is_on_logical_rendering_thread());
        const auto found = std::find_if(
            primitives_.begin(),
            primitives_.end(),
            [proxy](const std::unique_ptr<PrimitiveSceneInfo>& info)
            {
                return info->proxy() == proxy;
            });
        if (found == primitives_.end())
        {
            TOY_LOG_ERROR("RenderScene received a remove for an unregistered PrimitiveSceneProxy.");
            return;
        }

        std::unique_ptr<PrimitiveSceneInfo> removed = std::move(*found);
        primitives_.erase(found);
        removed.reset();
    }

    bool RenderScene::is_on_logical_rendering_thread() const
    {
        const NamedThread current_thread = task_graph_.get_current_thread_if_known();
        return current_thread != NamedThread::Unknown
            && current_thread == task_graph_.get_render_thread();
    }
}
