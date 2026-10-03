#pragma once

#include "assets/asset_resource_picker.h"

namespace toy3d
{
    bool draw_mesh_material_slot(AssetResourcePicker& picker, const EditorWorkspace& workspace, std::size_t index,
                                 const std::string& slot, const AssetRef& current, AssetRef& selected,
                                 std::string& error);
} // namespace toy3d
