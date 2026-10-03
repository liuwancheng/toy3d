#include "mesh_materials.h"

#include <algorithm>
#include <set>

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const std::string& message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, "default_materials", message, {}};
        }
    } // namespace

    AssetStatus validate_mesh_materials(const std::vector<std::string>& slots, const std::vector<AssetRef>& materials)
    {
        if (!materials.empty() && materials.size() != slots.size())
        {
            return invalid("Default material count differs from mesh slot count.");
        }
        for (const auto& reference : materials)
        {
            if (reference.subresource_id.valid() || reference.strength != AssetRefStrength::Strong ||
                (reference.asset_id.valid() ? reference.expected_type != "toy3d.MaterialAssetData" &&
                                                  reference.expected_type != "toy3d.MaterialInstanceAssetData"
                                            : !reference.expected_type.empty()))
            {
                return invalid("Mesh defaults require a Material/MaterialInstance or a canonical empty reference.");
            }
        }
        return AssetStatus::success();
    }

    std::vector<AssetRef> mesh_material_dependencies(const std::vector<AssetRef>& materials,
                                                     std::vector<AssetRef> dependencies)
    {
        for (const auto& reference : materials)
        {
            if (reference.asset_id.valid() && std::none_of(dependencies.begin(), dependencies.end(),
                                                           [&reference](const AssetRef& existing)
                                                           {
                                                               return existing.asset_id == reference.asset_id;
                                                           }))
            {
                dependencies.push_back(reference);
            }
        }
        return dependencies;
    }

    AssetStatus validate_mesh_material_dependencies(const std::vector<AssetRef>& materials,
                                                    const std::vector<AssetRef>& dependencies)
    {
        for (const auto& reference : materials)
        {
            if (reference.asset_id.valid() &&
                std::none_of(dependencies.begin(), dependencies.end(),
                             [&reference](const AssetRef& existing)
                             {
                                 return existing.asset_id == reference.asset_id &&
                                        existing.expected_type == reference.expected_type &&
                                        existing.strength == reference.strength &&
                                        existing.subresource_id == reference.subresource_id;
                             }))
            {
                return invalid("Mesh material reference is missing from the declared dependencies.");
            }
        }
        return AssetStatus::success();
    }

    ReflectionStatus register_mesh_material_migration(TypeRegistry& types, const std::string& root,
                                                      std::uint32_t previous_version)
    {
        return types.add_previous_schema(root, previous_version, {"default_materials"},
                                         [](SchemaFields& fields)
                                         {
                                             ValueWriter writer;
                                             // Old meshes had names only. An empty list explicitly means engine
                                             // defaults for all slots.
                                             const auto status = writer.write_array_length(0);
                                             if (status.succeeded())
                                             {
                                                 fields.emplace("default_materials", SchemaField{true, writer.bytes()});
                                             }
                                             return status;
                                         });
    }

    AssetResult<std::vector<AssetRef>> remap_mesh_materials(const std::vector<std::string>& old_slots,
                                                            const std::vector<AssetRef>& old_materials,
                                                            const std::vector<std::string>& new_slots)
    {
        const auto valid = validate_mesh_materials(old_slots, old_materials);
        if (!valid.succeeded())
        {
            return AssetResult<std::vector<AssetRef>>(valid);
        }
        if (old_materials.empty())
        {
            return AssetResult<std::vector<AssetRef>>(std::vector<AssetRef>{});
        }
        std::set<std::string> unique(new_slots.begin(), new_slots.end());
        if (unique.size() != new_slots.size())
        {
            return AssetResult<std::vector<AssetRef>>(invalid("Reimport has ambiguous material slot names."));
        }
        std::vector<AssetRef> result(new_slots.size());
        for (std::size_t i = 0; i < old_materials.size(); ++i)
        {
            if (!old_materials[i].asset_id.valid())
            {
                continue;
            }
            const auto found = std::find(new_slots.begin(), new_slots.end(), old_slots[i]);
            if (found == new_slots.end() || std::count(old_slots.begin(), old_slots.end(), old_slots[i]) != 1)
            {
                return AssetResult<std::vector<AssetRef>>(
                    invalid("Reimport would lose material assignment: " + old_slots[i]));
            }
            result[static_cast<std::size_t>(found - new_slots.begin())] = old_materials[i];
        }
        return AssetResult<std::vector<AssetRef>>(std::move(result));
    }
} // namespace toy3d
