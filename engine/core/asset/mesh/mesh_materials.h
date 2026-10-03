#pragma once

#include "asset/asset_pair.h"

namespace toy3d
{
    AssetStatus validate_mesh_materials(const std::vector<std::string>& slots, const std::vector<AssetRef>& materials);
    std::vector<AssetRef> mesh_material_dependencies(const std::vector<AssetRef>& materials,
                                                     std::vector<AssetRef> dependencies = {});
    AssetStatus validate_mesh_material_dependencies(const std::vector<AssetRef>& materials,
                                                    const std::vector<AssetRef>& dependencies);
    ReflectionStatus register_mesh_material_migration(TypeRegistry& types, const std::string& root,
                                                      std::uint32_t previous_version);
    AssetResult<std::vector<AssetRef>> remap_mesh_materials(const std::vector<std::string>& old_slots,
                                                            const std::vector<AssetRef>& old_materials,
                                                            const std::vector<std::string>& new_slots);
} // namespace toy3d
