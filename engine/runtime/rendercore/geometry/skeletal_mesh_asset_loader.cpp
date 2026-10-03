#include "rendercore/geometry/skeletal_mesh_asset_loader.h"

#include "asset/asset_pair.h"

namespace toy3d
{
    AssetResult<SkeletalMeshAssets> load_skeletal_mesh_assets(const TypeRegistry& types, const FileSystem& files,
                                                              const AssetIndex& index,
                                                              const SceneSkeletalMeshData& data,
                                                              const MaterialInterfaceRef& default_material)
    {
        SceneComponentData component;
        component.type = "toy3d.SkeletalMeshComponent";
        component.properties = data;
        if (!validate_component_data(component))
        {
            return AssetResult<SkeletalMeshAssets>(AssetStatus{
                AssetErrorCode::Value, {}, {}, {}, {}, "Invalid skeletal mesh binding or playback settings.", {}});
        }
        SkeletalMeshAssets result;
        AssetRef mesh_ref;
        AssetRef animation_ref;
        for (const auto& resource : data.resources)
        {
            if (resource.role == "mesh")
            {
                mesh_ref = resource.reference;
            }
            else if (resource.role == "animation")
            {
                animation_ref = resource.reference;
            }
        }
        if (!mesh_ref.asset_id.valid())
        {
            return AssetResult<SkeletalMeshAssets>(std::move(result));
        }
        const auto read = [&](const AssetRef& ref) -> AssetResult<AssetPair>
        {
            const auto resolved = index.resolve(ref, "skeletal mesh binding");
            if (!resolved.succeeded())
            {
                return AssetResult<AssetPair>(resolved);
            }
            const auto* location = index.find(ref.asset_id);
            auto pair = read_asset_pair(types, files, location->path);
            if (pair.succeeded() && (!(pair.value().description.index.asset_id == ref.asset_id) ||
                                     pair.value().description.index.root_type != ref.expected_type))
            {
                return AssetResult<AssetPair>(AssetStatus{AssetErrorCode::Value,
                                                          {},
                                                          {},
                                                          {},
                                                          {},
                                                          "Skeletal mesh binding identity/type changed while loading.",
                                                          {}});
            }
            return pair;
        };
        const auto mesh_pair = read(mesh_ref);
        if (!mesh_pair.succeeded())
        {
            return AssetResult<SkeletalMeshAssets>(mesh_pair.status());
        }
        auto mesh = decode_skeletal_mesh_asset_pair(mesh_pair.value());
        if (!mesh.succeeded())
        {
            return AssetResult<SkeletalMeshAssets>(mesh.status());
        }
        const auto skeleton_pair = read(mesh.value().data.skeleton);
        if (!skeleton_pair.succeeded())
        {
            return AssetResult<SkeletalMeshAssets>(skeleton_pair.status());
        }
        const auto skeleton = decode_skeleton_asset_pair(skeleton_pair.value());
        if (!skeleton.succeeded())
        {
            return AssetResult<SkeletalMeshAssets>(skeleton.status());
        }
        auto layout =
            std::make_shared<const AnimationBoneLayout>(mesh.value().data.skeleton.asset_id, skeleton.value());
        std::vector<MaterialInterfaceRef> materials(mesh.value().data.material_slots.size(), default_material);
        auto created = SkeletalMesh::create(layout, std::move(mesh).value(), std::move(materials));
        if (!created.succeeded())
        {
            return AssetResult<SkeletalMeshAssets>(created.status());
        }
        result.mesh = std::move(created).value();
        if (animation_ref.asset_id.valid())
        {
            const auto pair = read(animation_ref);
            if (!pair.succeeded())
            {
                return AssetResult<SkeletalMeshAssets>(pair.status());
            }
            const auto animation = decode_animation_sequence_asset_pair(pair.value());
            if (!animation.succeeded())
            {
                return AssetResult<SkeletalMeshAssets>(animation.status());
            }
            result.sequence = std::make_shared<const AnimationSequence>(layout, animation.value());
            if (!result.sequence->status().succeeded())
            {
                return AssetResult<SkeletalMeshAssets>(result.sequence->status());
            }
        }
        return AssetResult<SkeletalMeshAssets>(std::move(result));
    }
} // namespace toy3d
