#include "renderscene/render_scene.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/scene/skeletal_mesh_scene_proxy.h"
#include "rendercore/material/material_render_proxy.h"
#include "renderscene/primitive_scene_info.h"
#include "rendercore/render_resource_manager.h"
#include "threading/task_graph/task_graph_interface.h"

namespace toy3d
{
    RenderScene::RenderScene(TaskGraphInterface& task_graph, RenderResourceManager& resource_manager)
        : task_graph_(task_graph), resource_manager_(resource_manager)
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

    void RenderScene::add_primitive(std::unique_ptr<PrimitiveSceneProxy> proxy)
    {
        enqueue_render_command("AddPrimitive",
                               [this, proxy = std::move(proxy)]() mutable noexcept
                               {
                                   add_primitive_render_thread(std::move(proxy));
                               });
    }

    void RenderScene::update_primitive_transform(PrimitiveSceneProxy* proxy, Matrix4 world_transform,
                                                 AxisAlignedBounds world_bounds, bool visible, bool cast_shadows,
                                                 bool receives_shadows)
    {
        enqueue_render_command(
            "UpdatePrimitiveTransform",
            [this, proxy, world_transform = std::move(world_transform), world_bounds = std::move(world_bounds), visible,
             cast_shadows, receives_shadows]() mutable noexcept
            {
                update_primitive_transform_render_thread(proxy, std::move(world_transform), std::move(world_bounds),
                                                         visible, cast_shadows, receives_shadows);
            });
    }

    void RenderScene::update_primitive_materials(PrimitiveSceneProxy* proxy,
                                                 std::vector<MaterialRenderProxy*> materials)
    {
        enqueue_render_command(
            "UpdatePrimitiveMaterials",
            [this, proxy, materials = std::move(materials)]() mutable noexcept
            {
                assert(is_on_logical_rendering_thread());
                const auto found = std::find_if(primitives_.begin(), primitives_.end(),
                                                [proxy](const std::unique_ptr<PrimitiveSceneInfo>& info)
                                                {
                                                    return info->proxy() == proxy;
                                                });
                auto* mesh = found == primitives_.end() ? nullptr : (*found)->proxy();
                if (!mesh || materials.size() != mesh->material_render_proxies().size())
                {
                    TOY_LOG_ERROR("Material update requires a registered primitive with matching slots.");
                    return;
                }
                for (auto* material : materials)
                {
                    if (!material)
                    {
                        TOY_LOG_ERROR("Material update contains a null render proxy.");
                        return;
                    }
                    const auto status = material->begin_init_textures(resource_manager_);
                    if (!status)
                    {
                        TOY_LOG_ERROR("Material update could not initialize textures: {}", status.message());
                        return;
                    }
                }
                // Material changes do not end the geometry lifetime. Removing the
                // last Primitive would terminally release shared mesh resources.
                mesh->set_material_render_proxies(std::move(materials));
            });
    }

    void RenderScene::update_skeletal_mesh_pose(PrimitiveSceneProxy* proxy,
                                                std::shared_ptr<const SkeletalMeshDeformationData> deformation,
                                                Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                                                bool cast_shadows, bool receives_shadows)
    {
        enqueue_render_command(
            "UpdateSkeletalMeshPose",
            [this, proxy, deformation = std::move(deformation), world_transform, world_bounds, visible, cast_shadows,
             receives_shadows]() noexcept
            {
                assert(is_on_logical_rendering_thread());
                const auto found = std::find_if(primitives_.begin(), primitives_.end(),
                                                [proxy](const std::unique_ptr<PrimitiveSceneInfo>& info)
                                                {
                                                    return info->proxy() == proxy;
                                                });
                auto* mesh =
                    found == primitives_.end() ? nullptr : dynamic_cast<SkeletalMeshSceneProxy*>((*found)->proxy());
                if (!mesh)
                {
                    TOY_LOG_ERROR("Pose update requires a registered SkeletalMesh proxy.");
                    return;
                }
                const auto status = mesh->set_deformation(deformation, resource_manager_);
                if (!status)
                {
                    TOY_LOG_ERROR("Skeletal pose update failed: {}", status.message());
                    return;
                }
                // Pose and its matching world bounds become visible together in this FIFO command.
                mesh->update_transform(world_transform, world_bounds, visible, cast_shadows, receives_shadows);
            });
    }

    void RenderScene::remove_primitive(PrimitiveSceneProxy* proxy)
    {
        enqueue_render_command("RemovePrimitive",
                               [this, proxy]() noexcept
                               {
                                   remove_primitive_render_thread(proxy);
                               });
    }

    void RenderScene::add_primitive_render_thread(std::unique_ptr<PrimitiveSceneProxy> proxy) noexcept
    {
        assert(is_on_logical_rendering_thread());
        if (!proxy)
        {
            TOY_LOG_ERROR("RenderScene cannot add a null PrimitiveSceneProxy.");
            return;
        }

        PrimitiveSceneProxy* const proxy_identity = proxy.get();
        const auto duplicate = std::find_if(primitives_.begin(), primitives_.end(),
                                            [proxy_identity](const std::unique_ptr<PrimitiveSceneInfo>& info)
                                            {
                                                return info->proxy() == proxy_identity;
                                            });
        if (duplicate != primitives_.end())
        {
            TOY_LOG_ERROR("RenderScene received a duplicate PrimitiveSceneProxy add.");
            return;
        }

        const auto init_status = proxy_identity->begin_init_resources(resource_manager_);
        if (!init_status)
        {
            preparation_error_ = init_status;
            TOY_LOG_ERROR("RenderScene could not initialize primitive resources: {}", init_status.message());
        }
        for (MaterialRenderProxy* const material : proxy_identity->material_render_proxies())
        {
            if (!material)
            {
                continue;
            }
            const auto status = material->begin_init_textures(resource_manager_);
            if (!status)
            {
                preparation_error_ = status;
                TOY_LOG_ERROR("RenderScene could not initialize Material textures: {}", status.message());
            }
        }

        primitives_.push_back(std::make_unique<PrimitiveSceneInfo>(std::move(proxy)));
    }

    void RenderScene::update_primitive_transform_render_thread(PrimitiveSceneProxy* proxy, Matrix4 world_transform,
                                                               AxisAlignedBounds world_bounds, bool visible,
                                                               bool cast_shadows, bool receives_shadows) noexcept
    {
        assert(is_on_logical_rendering_thread());
        const auto found = std::find_if(primitives_.begin(), primitives_.end(),
                                        [proxy](const std::unique_ptr<PrimitiveSceneInfo>& info)
                                        {
                                            return info->proxy() == proxy;
                                        });
        if (found == primitives_.end())
        {
            TOY_LOG_ERROR("RenderScene received an update for an unregistered PrimitiveSceneProxy.");
            return;
        }

        (*found)->proxy()->update_transform(std::move(world_transform), std::move(world_bounds), visible, cast_shadows,
                                            receives_shadows);
    }

    void RenderScene::remove_primitive_render_thread(PrimitiveSceneProxy* proxy) noexcept
    {
        assert(is_on_logical_rendering_thread());
        const auto found = std::find_if(primitives_.begin(), primitives_.end(),
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
        const auto still_referenced = std::find_if(
            primitives_.begin(), primitives_.end(),
            [&removed](const std::unique_ptr<PrimitiveSceneInfo>& info)
            {
                return info && info->proxy() && removed->proxy()->shares_geometry_resources(*info->proxy());
            });
        const auto status =
            removed->proxy()->release_resources(resource_manager_, still_referenced == primitives_.end());
        if (!status)
        {
            TOY_LOG_ERROR("RenderScene could not release primitive resources: {}", status.message());
        }

        removed.reset();
        if (primitives_.empty())
        {
            preparation_error_ = RHIStatus::success();
        }
    }

    RHIStatus RenderScene::preparation_status() const
    {
        assert(is_on_logical_rendering_thread());
        if (!preparation_error_)
        {
            return preparation_error_;
        }
        for (const auto& primitive : primitives_)
        {
            if (primitive->proxy() && !primitive->proxy()->resources_drawable())
            {
                return RHIStatus::failure(RHIErrorCode::NotReady, "Scene geometry is preparing.");
            }
        }
        return RHIStatus::success();
    }

    void RenderScene::add_light(std::unique_ptr<LightSceneProxy> proxy)
    {
        enqueue_render_command("AddLight",
                               [this, proxy = std::move(proxy)]() mutable noexcept
                               {
                                   assert(is_on_logical_rendering_thread());
                                   if (!proxy)
                                   {
                                       TOY_LOG_ERROR("Null light proxy.");
                                       return;
                                   }
                                   lights_.push_back(std::move(proxy));
                               });
    }

    void RenderScene::update_light(LightSceneProxy* proxy, LightSceneData data)
    {
        enqueue_render_command("UpdateLight",
                               [this, proxy, data]() noexcept
                               {
                                   assert(is_on_logical_rendering_thread());
                                   for (const auto& light : lights_)
                                   {
                                       if (light.get() == proxy)
                                       {
                                           light->data = data;
                                           return;
                                       }
                                   }
                                   TOY_LOG_ERROR("Update for an unregistered light proxy.");
                               });
    }

    void RenderScene::remove_light(LightSceneProxy* proxy)
    {
        enqueue_render_command("RemoveLight",
                               [this, proxy]() noexcept
                               {
                                   assert(is_on_logical_rendering_thread());
                                   const auto found =
                                       std::find_if(lights_.begin(), lights_.end(),
                                                    [proxy](const std::unique_ptr<LightSceneProxy>& light)
                                                    {
                                                        return light.get() == proxy;
                                                    });
                                   if (found == lights_.end())
                                   {
                                       TOY_LOG_ERROR("Remove for an unregistered light proxy.");
                                       return;
                                   }
                                   lights_.erase(found);
                               });
    }

    const std::vector<std::unique_ptr<LightSceneProxy>>& RenderScene::lights() const
    {
        assert(is_on_logical_rendering_thread());
        return lights_;
    }

    bool RenderScene::is_on_logical_rendering_thread() const
    {
        const NamedThread current_thread = task_graph_.get_current_thread_if_known();
        return current_thread != NamedThread::Unknown && current_thread == task_graph_.get_render_thread();
    }
} // namespace toy3d
