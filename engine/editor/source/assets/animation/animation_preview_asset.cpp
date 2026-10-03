#include "assets/animation/animation_preview_asset.h"

#include <utility>
#include <optional>

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(std::string message)
        {
            AssetStatus status;
            status.code = AssetErrorCode::InvalidFormat;
            status.message = std::move(message);
            return status;
        }

        bool matches_source(const AnimationPreviewAsset* cached, const AssetId& id, const VirtualPath& path,
                            const AssetPair& pair)
        {
            if (cached)
            {
                for (const auto& source : cached->sources)
                {
                    if (source.id == id && source.path.utf8() == path.utf8() &&
                        source.description == pair.description_bytes)
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    } // namespace

    bool animation_asset_uses_skeleton(const AssetFileIndex& asset, const AssetId& skeleton)
    {
        for (const auto& dependency : asset.dependencies)
        {
            if (dependency.asset_id == skeleton && dependency.expected_type == "toy3d.SkeletonAssetData")
            {
                return true;
            }
        }
        return false;
    }

    AssetResult<AnimationPreviewAsset> load_animation_preview_asset(
        AssetPairStore& pairs, const AssetCatalog& catalog, const AssetId& id, bool override_selection,
        const AssetId& selected_mesh, const AssetId& selected_sequence,
        std::shared_ptr<const AnimationPreviewAsset> reusable)
    {
        AnimationPreviewAsset result;
        const auto* location = catalog.index.find(id);
        if (!location)
        {
            return AssetResult<AnimationPreviewAsset>(invalid("Preview asset is missing from the catalog."));
        }
        const auto primary = pairs.read(location->path);
        if (!primary.succeeded())
        {
            return AssetResult<AnimationPreviewAsset>(primary.status());
        }
        if (!(primary.value().description.index.asset_id == id) ||
            primary.value().description.index.root_type != location->index.root_type)
        {
            return AssetResult<AnimationPreviewAsset>(invalid("Preview identity/type changed during load."));
        }
        result.id = id;
        result.path = location->path.utf8();
        result.root_type = location->index.root_type;
        result.sources.push_back({id, location->path, primary.value().description_bytes});
        AssetId skeleton_id;
        std::shared_ptr<const SkeletalMeshAsset> primary_mesh;
        std::shared_ptr<const AnimationSequenceAsset> primary_sequence;
        if (result.root_type == "toy3d.SkeletonAssetData")
        {
            skeleton_id = id;
        }
        else if (result.root_type == "toy3d.SkeletalMeshAssetData")
        {
            if (reusable && reusable->mesh_id == id && reusable->mesh &&
                matches_source(reusable.get(), id, location->path, primary.value()))
            {
                primary_mesh = reusable->mesh;
            }
            else
            {
                auto mesh = decode_skeletal_mesh_asset_pair(primary.value());
                if (!mesh.succeeded())
                {
                    return AssetResult<AnimationPreviewAsset>(mesh.status());
                }
                primary_mesh = std::make_shared<const SkeletalMeshAsset>(std::move(mesh).value());
            }
            skeleton_id = primary_mesh->data.skeleton.asset_id;
            if (!override_selection)
            {
                result.mesh_id = id;
            }
        }
        else if (result.root_type == "toy3d.AnimationSequenceAssetData")
        {
            auto sequence = decode_animation_sequence_asset_pair(primary.value());
            if (!sequence.succeeded())
            {
                return AssetResult<AnimationPreviewAsset>(sequence.status());
            }
            skeleton_id = sequence.value().data.skeleton.asset_id;
            primary_sequence = std::make_shared<const AnimationSequenceAsset>(std::move(sequence).value());
            if (!override_selection)
            {
                result.sequence_id = id;
                for (const auto& entry : catalog.entries)
                {
                    if (entry.file.root_type == "toy3d.SkeletalMeshAssetData" &&
                        animation_asset_uses_skeleton(entry.file, skeleton_id))
                    {
                        result.mesh_id = entry.file.asset_id;
                        break;
                    }
                }
            }
        }
        else
        {
            return AssetResult<AnimationPreviewAsset>(invalid("Unsupported animation preview asset type."));
        }
        if (override_selection)
        {
            result.mesh_id = selected_mesh;
            result.sequence_id = selected_sequence;
        }
        const auto* skeleton_location = catalog.index.find(skeleton_id);
        if (!skeleton_location || skeleton_location->index.root_type != "toy3d.SkeletonAssetData")
        {
            return AssetResult<AnimationPreviewAsset>(invalid("Preview Skeleton is missing."));
        }
        // Keep the primary pair by reference: its meta payload can contain the whole mesh.
        // C++17 optional holds only the additional read; the primary pair is never copied.
        std::optional<AssetResult<AssetPair>> skeleton_read;
        if (!(skeleton_id == id))
        {
            skeleton_read.emplace(pairs.read(skeleton_location->path));
        }
        const auto& skeleton_pair = skeleton_id == id ? primary : *skeleton_read;
        if (!skeleton_pair.succeeded())
        {
            return AssetResult<AnimationPreviewAsset>(skeleton_pair.status());
        }
        const auto skeleton = decode_skeleton_asset_pair(skeleton_pair.value());
        if (!skeleton.succeeded() || !(skeleton_pair.value().description.index.asset_id == skeleton_id))
        {
            return AssetResult<AnimationPreviewAsset>(invalid("Preview Skeleton identity/data is invalid."));
        }
        if (!(skeleton_id == id))
        {
            result.sources.push_back({skeleton_id, skeleton_location->path, skeleton_pair.value().description_bytes});
        }
        const bool reuse_layout =
            reusable && reusable->layout && reusable->layout->skeleton_id() == skeleton_id &&
            matches_source(reusable.get(), skeleton_id, skeleton_location->path, skeleton_pair.value());
        result.layout = reuse_layout ? reusable->layout
                                     : std::make_shared<const AnimationBoneLayout>(skeleton_id, skeleton.value());
        if (!result.layout->status().succeeded())
        {
            return AssetResult<AnimationPreviewAsset>(result.layout->status());
        }
        // The opened asset itself must remain valid even when the user selects
        // reference pose or a different compatible preview mesh.
        AssetStatus primary_status;
        if (primary_mesh && !(reuse_layout && reusable->mesh == primary_mesh))
        {
            primary_status = validate_skeletal_mesh_compatibility(*primary_mesh, skeleton_id, skeleton.value());
        }
        else if (primary_sequence)
        {
            primary_status = validate_animation_compatibility(*primary_sequence, skeleton_id, skeleton.value());
        }
        if (!primary_status.succeeded())
        {
            return AssetResult<AnimationPreviewAsset>(primary_status);
        }
        // Selections require exact identity and reference hash; sharing a name is insufficient.
        for (const auto& selected : {result.mesh_id, result.sequence_id})
        {
            if (!selected.valid())
            {
                continue;
            }
            const auto* selected_location = catalog.index.find(selected);
            if (!selected_location)
            {
                return AssetResult<AnimationPreviewAsset>(invalid("Preview selection was removed."));
            }
            // C++17 optional owns a separate selection read without duplicating the primary payload.
            std::optional<AssetResult<AssetPair>> selected_read;
            if (!(selected == id))
            {
                selected_read.emplace(pairs.read(selected_location->path));
            }
            const auto& pair = selected == id ? primary : *selected_read;
            if (!pair.succeeded())
            {
                return AssetResult<AnimationPreviewAsset>(pair.status());
            }
            if (!(pair.value().description.index.asset_id == selected))
            {
                return AssetResult<AnimationPreviewAsset>(invalid("Preview selection identity changed."));
            }
            AssetStatus status;
            const bool reuse_selected =
                reuse_layout && matches_source(reusable.get(), selected, selected_location->path, pair.value());
            if (selected == result.mesh_id)
            {
                auto mesh = selected == id ? primary_mesh : std::shared_ptr<const SkeletalMeshAsset>();
                if (!mesh && reuse_selected && reusable->mesh_id == selected)
                {
                    mesh = reusable->mesh;
                }
                if (!mesh)
                {
                    auto decoded = decode_skeletal_mesh_asset_pair(pair.value());
                    if (!decoded.succeeded())
                    {
                        return AssetResult<AnimationPreviewAsset>(decoded.status());
                    }
                    mesh = std::make_shared<const SkeletalMeshAsset>(std::move(decoded).value());
                }
                if (!(reuse_layout && reusable->mesh == mesh))
                {
                    status = validate_skeletal_mesh_compatibility(*mesh, skeleton_id, skeleton.value());
                }
                result.mesh = std::move(mesh);
            }
            else
            {
                if (reuse_selected && reusable->sequence_id == selected && reusable->sequence)
                {
                    result.sequence = reusable->sequence;
                    result.sample_rate = reusable->sample_rate;
                    if (!(selected == id))
                    {
                        result.sources.push_back({selected, selected_location->path, pair.value().description_bytes});
                    }
                    continue;
                }
                auto sequence = selected == id ? primary_sequence : std::shared_ptr<const AnimationSequenceAsset>();
                if (!sequence)
                {
                    auto decoded = decode_animation_sequence_asset_pair(pair.value());
                    if (!decoded.succeeded())
                    {
                        return AssetResult<AnimationPreviewAsset>(decoded.status());
                    }
                    sequence = std::make_shared<const AnimationSequenceAsset>(std::move(decoded).value());
                    status = validate_animation_compatibility(*sequence, skeleton_id, skeleton.value());
                }
                result.sample_rate = sequence->data.sample_rate;
                result.sequence = std::make_shared<const AnimationSequence>(result.layout, *sequence);
                if (status.succeeded())
                {
                    status = result.sequence->status();
                }
            }
            if (!status.succeeded())
            {
                return AssetResult<AnimationPreviewAsset>(status);
            }
            if (!(selected == id))
            {
                result.sources.push_back({selected, selected_location->path, pair.value().description_bytes});
            }
        }
        return AssetResult<AnimationPreviewAsset>(std::move(result));
    }

    bool animation_preview_asset_current(AssetPairStore& pairs, const AssetCatalog& catalog,
                                         const AnimationPreviewAsset& asset)
    {
        for (const auto& source : asset.sources)
        {
            const auto* location = catalog.index.find(source.id);
            if (!location || location->path.utf8() != source.path.utf8())
            {
                return false;
            }
            const auto current = pairs.read(source.path);
            if (!current.succeeded() || current.value().description_bytes != source.description)
            {
                return false;
            }
        }
        return true;
    }
} // namespace toy3d
