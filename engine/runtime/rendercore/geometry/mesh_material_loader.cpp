#include "mesh_material_loader.h"

namespace toy3d
{
    AssetResult<std::vector<MaterialInterfaceRef>> load_mesh_materials(const std::vector<std::string>& slots,
                                                                       const std::vector<AssetRef>& references,
                                                                       const MaterialInterfaceRef& fallback,
                                                                       const MeshMaterialResolver& resolver,
                                                                       shader::VertexFactoryType factory,
                                                                       bool valid_tangents)
    {
        using Result = AssetResult<std::vector<MaterialInterfaceRef>>;
        const auto valid = validate_mesh_materials(slots, references);
        if (!valid.succeeded())
        {
            return Result(valid);
        }
        std::vector<MaterialInterfaceRef> result(slots.size(), fallback);
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            if (!references.empty() && references[i].asset_id.valid())
            {
                if (!resolver)
                {
                    return Result(AssetStatus{AssetErrorCode::InvalidState,
                                              {},
                                              {},
                                              {},
                                              slots[i],
                                              "Mesh material resolver is unavailable.",
                                              {}});
                }
                const auto material = resolver(references[i]);
                if (!material.succeeded())
                {
                    return Result(material.status());
                }
                result[i] = material.value();
            }
            std::string error;
            const bool assigned = !references.empty() && references[i].asset_id.valid();
            if (!result[i] || !validate_material_geometry(result[i]->desc(), factory, true, valid_tangents, error) ||
                (assigned &&
                 (!validate_material_mesh_pass(result[i]->desc(), shader::ShaderPassRole::HitProxy, factory, error) ||
                  !validate_material_mesh_pass(result[i]->desc(), shader::ShaderPassRole::ShadowDepth, factory,
                                               error))))
            {
                return Result(AssetStatus{
                    AssetErrorCode::Value, {}, {}, {}, slots[i], "Mesh material slot " + slots[i] + ": " + error, {}});
            }
        }
        return Result(std::move(result));
    }
} // namespace toy3d
