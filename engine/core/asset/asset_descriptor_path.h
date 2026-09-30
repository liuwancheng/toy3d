#pragma once

#include "file_system/virtual_path.h"

#include <string>

namespace toy3d
{
    enum class AssetDescriptorKind { Invalid, Asset, Scene };

    AssetDescriptorKind asset_descriptor_kind(const VirtualPath& path);
    bool asset_descriptor_accepts_type(AssetDescriptorKind kind, const std::string& root_type);
    bool asset_meta_path(const VirtualPath& descriptor, VirtualPath& output);
}
