#pragma once

#include "asset/edit_session.h"
#include "asset/mesh/skeletal_mesh_asset.h"

namespace toy3d
{
    class EditorWorkspace;
    // One owner-thread draft for either mesh descriptor. Geometry and opaque meta segments are preserved.
    class MeshMaterialEditSession final
    {
      public:
        using Prepare = std::function<AssetStatus(const std::vector<AssetRef>&)>;
        explicit MeshMaterialEditSession(EditorWorkspace& workspace);
        AssetStatus open(const AssetId& id, Prepare prepare);
        void clear();
        bool active() const;
        bool writable() const;
        bool dirty() const;
        const AssetId& id() const;
        const std::vector<std::string>& slots() const;
        const std::vector<AssetRef>& materials() const;
        AssetStatus set_material(std::size_t slot, const AssetRef& material);
        AssetStatus undo();
        AssetStatus redo();
        AssetStatus save();

      private:
        EditorWorkspace& workspace_;
        AssetId id_;
        VirtualPath path_;
        AssetPair opened_;
        std::vector<AssetRef> saved_materials_;
        std::unique_ptr<EditSession<StaticMeshAssetData>> static_;
        std::unique_ptr<EditSession<SkeletalMeshAssetData>> skeletal_;
    };
} // namespace toy3d
