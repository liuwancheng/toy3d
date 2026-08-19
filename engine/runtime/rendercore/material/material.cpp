#include "rendercore/material/material.h"

#include "logging/logger.h"

#include <utility>

namespace toy3d
{
    std::shared_ptr<const Material> Material::create(MaterialDesc desc)
    {
        if (desc.shader_name.empty())
        {
            TOY_LOG_ERROR("A Material must identify a ShaderMap shader.");
            return nullptr;
        }
        Material material(std::move(desc));
        return std::make_shared<Material>(std::move(material));
    }

    Material::Material(MaterialDesc desc) : desc_(std::move(desc))
    {
    }

    std::shared_ptr<MaterialInstance> MaterialInstance::create(MaterialRef material)
    {
        if (material == nullptr)
        {
            TOY_LOG_ERROR("A MaterialInstance must reference a Material.");
            return nullptr;
        }
        MaterialInstance material_instance(std::move(material));
        return std::make_shared<MaterialInstance>(std::move(material_instance));
    }

    MaterialInstance::MaterialInstance(MaterialRef material)
        : material_(std::move(material)),
          render_resource_id_(allocate_render_id<MaterialRenderResourceId>())
    {
    }

    MaterialInstance::MaterialInstance(MaterialInstance&& other) noexcept
        : material_(std::move(other.material_)),
          render_resource_id_(other.render_resource_id_),
          revision_(other.revision_)
    {
        // Resource identity follows the moved value so two live objects never
        // advertise the same ID during factory construction or later moves.
        other.render_resource_id_ = {};
    }
}
