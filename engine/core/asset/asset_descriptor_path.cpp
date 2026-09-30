#include "asset_descriptor_path.h"

namespace toy3d
{
    AssetDescriptorKind asset_descriptor_kind(const VirtualPath& path)
    {
        const std::string& name = path.utf8();
        if (name.size() > 6u && name.compare(name.size() - 6u, 6u, ".asset") == 0)
            return AssetDescriptorKind::Asset;
        if (name.size() > 6u && name.compare(name.size() - 6u, 6u, ".scene") == 0)
            return AssetDescriptorKind::Scene;
        return AssetDescriptorKind::Invalid;
    }

    bool asset_descriptor_accepts_type(AssetDescriptorKind kind, const std::string& root_type)
    {
        constexpr const char* scene_type = "toy3d.SceneAssetData";
        return (kind == AssetDescriptorKind::Scene && root_type == scene_type) ||
               (kind == AssetDescriptorKind::Asset && root_type != scene_type);
    }

    bool asset_meta_path(const VirtualPath& descriptor, VirtualPath& output)
    {
        const AssetDescriptorKind kind = asset_descriptor_kind(descriptor);
        if (kind == AssetDescriptorKind::Invalid) return false;
        const std::string& name = descriptor.utf8();
        // Scene's reserved sidecar retains the descriptor extension so it cannot
        // collide with the legacy .asset -> .meta pairing for the same stem.
        const std::string paired = kind == AssetDescriptorKind::Scene ? name + ".meta" :
            name.substr(0u, name.size() - 6u) + ".meta";
        const auto parsed = VirtualPath::parse(paired);
        if (!parsed.succeeded()) return false;
        output = parsed.value();
        return true;
    }
}
