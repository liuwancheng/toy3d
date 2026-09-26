#include "gamescene/component/primitive_component.h"

#include <cassert>
#include <memory>
#include <utility>

#include "gamescene/world/world.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/scene_interface.h"

namespace toy3d
{
    PrimitiveComponent::~PrimitiveComponent()
    {
        // Actor unregister is the normal release path and must run while the World
        // still exposes its SceneInterface shutdown window.
        assert(scene_proxy_ == nullptr);
    }

    void PrimitiveComponent::set_visible(bool visible)
    {
        if (visible_ == visible)
        {
            return;
        }
        visible_ = visible;
        send_render_transform();
    }

    void PrimitiveComponent::create_render_state()
    {
        if (scene_proxy_ != nullptr)
        {
            return;
        }

        SceneInterface* const scene = world().scene_interface();
        if (scene == nullptr)
        {
            return;
        }

        std::unique_ptr<PrimitiveSceneProxy> proxy = create_scene_proxy();
        if (!proxy)
        {
            return;
        }

        PrimitiveSceneProxy* const proxy_identity = proxy.get();
        scene->add_primitive(std::move(proxy));
        // The fire-and-forget contract guarantees ownership has been consumed inline
        // or accepted by Task Graph before normal return.
        scene_proxy_ = proxy_identity;
        world().mark_scene_changed();
    }

    void PrimitiveComponent::send_render_transform()
    {
        SceneInterface* const scene = world().scene_interface();
        if (scene == nullptr || scene_proxy_ == nullptr)
        {
            return;
        }

        scene->update_primitive_transform(scene_proxy_, world_transform(), world_bounds_, visible_);
        world().mark_scene_changed();
    }

    void PrimitiveComponent::destroy_render_state()
    {
        if (scene_proxy_ == nullptr)
        {
            return;
        }

        SceneInterface* const scene = world().scene_interface();
        assert(scene != nullptr);
        scene->remove_primitive(scene_proxy_);
        scene_proxy_ = nullptr;
        world().mark_scene_changed();
    }

    void PrimitiveComponent::on_register()
    {
        create_render_state();
    }

    void PrimitiveComponent::on_unregister()
    {
        destroy_render_state();
    }

    void PrimitiveComponent::on_world_transform_updated()
    {
        update_bounds();
        send_render_transform();
    }
} // namespace toy3d
