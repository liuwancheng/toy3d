#include "gamescene/render_resource_update_collector.h"

#include <utility>

#include "gamescene/world.h"

namespace toy3d
{
    namespace
    {
        MeshRenderResourceVersionRef make_mesh_version(const StaticMesh& mesh)
        {
            auto version = std::make_shared<MeshRenderResourceVersion>();
            version->resource_id = mesh.render_resource_id();
            version->revision = mesh.revision();
            version->vertices = mesh.vertices();
            version->indices = mesh.indices();
            version->sections = mesh.sections();
            return version;
        }

        MaterialRenderResourceVersionRef make_material_version(
            const MaterialInstance& material)
        {
            auto version = std::make_shared<MaterialRenderResourceVersion>();
            version->resource_id = material.render_resource_id();
            version->revision = material.revision();
            version->material = material.material()->desc();
            return version;
        }
    }

    std::vector<RenderResourceUpdate> RenderResourceUpdateCollector::collect(
        const std::vector<std::reference_wrapper<const World>>& worlds)
    {
        current_meshes_.clear();
        current_materials_.clear();
        for (const std::reference_wrapper<const World>& world : worlds)
        {
            world.get().append_render_resources(*this);
        }

        std::vector<RenderResourceUpdate> updates;
        for (const auto& material_entry : current_materials_)
        {
            const MaterialInstanceRef& material = material_entry.second;
            const auto published = published_materials_.find(material_entry.first);
            if (published == published_materials_.end() ||
                published->second != material->revision())
            {
                MaterialRenderResourceUpdate update;
                update.resource_id = material->render_resource_id();
                update.version = make_material_version(*material);
                updates.push_back(std::move(update));
            }
        }
        for (const auto& mesh_entry : current_meshes_)
        {
            const StaticMeshRef& mesh = mesh_entry.second;
            const auto published = published_meshes_.find(mesh_entry.first);
            if (published == published_meshes_.end() ||
                published->second != mesh->revision())
            {
                MeshRenderResourceUpdate update;
                update.resource_id = mesh->render_resource_id();
                update.version = make_mesh_version(*mesh);
                updates.push_back(std::move(update));
            }
        }

        for (const auto& mesh_entry : published_meshes_)
        {
            if (current_meshes_.find(mesh_entry.first) == current_meshes_.end())
            {
                MeshRenderResourceUpdate update;
                update.operation = RenderResourceUpdateOperation::Release;
                update.resource_id = MeshRenderResourceId(mesh_entry.first);
                updates.push_back(std::move(update));
            }
        }
        for (const auto& material_entry : published_materials_)
        {
            if (current_materials_.find(material_entry.first) ==
                current_materials_.end())
            {
                MaterialRenderResourceUpdate update;
                update.operation = RenderResourceUpdateOperation::Release;
                update.resource_id = MaterialRenderResourceId(material_entry.first);
                updates.push_back(std::move(update));
            }
        }

        published_meshes_.clear();
        for (const auto& mesh_entry : current_meshes_)
        {
            published_meshes_.emplace(mesh_entry.first, mesh_entry.second->revision());
        }
        published_materials_.clear();
        for (const auto& material_entry : current_materials_)
        {
            published_materials_.emplace(
                material_entry.first, material_entry.second->revision());
        }
        return updates;
    }

    void RenderResourceUpdateCollector::add_mesh(const StaticMeshRef& mesh)
    {
        if (mesh != nullptr && mesh->render_resource_id())
        {
            current_meshes_.emplace(mesh->render_resource_id().value(), mesh);
        }
    }

    void RenderResourceUpdateCollector::add_material(
        const MaterialInstanceRef& material)
    {
        if (material != nullptr && material->render_resource_id())
        {
            current_materials_.emplace(
                material->render_resource_id().value(), material);
        }
    }
}
