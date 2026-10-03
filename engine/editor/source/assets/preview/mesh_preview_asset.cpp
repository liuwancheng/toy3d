#include "assets/preview/mesh_preview_asset.h"

#include <utility>
#include <optional>
#include <algorithm>

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

        bool matches_source(const MeshPreviewAsset* cached, const AssetId& id, const VirtualPath& path,
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

    AssetResult<AssetId> animation_preview_mesh_preference(const FileSystem& files, const AssetId& animation)
    {
        const auto path = VirtualPath::parse("/Saved/Editor/AnimationPreviewMeshes/" + animation.hex() + ".settings");
        const auto bytes = files.read_binary(path.value(), 1024);
        if (!bytes.succeeded())
        {
            if (bytes.status().code == FileErrorCode::NotFound)
            {
                return AssetResult<AssetId>(AssetId{});
            }
            return AssetResult<AssetId>(invalid(bytes.status().message));
        }
        ValueReader reader(bytes.value());
        std::uint32_t version = 0;
        std::string identity;
        AssetId mesh;
        if (!reader.read_uint32(version).succeeded() || version != 1 || !reader.read_utf8(identity).succeeded() ||
            !reader.at_end() || !AssetId::parse(identity, mesh) || !mesh.valid())
        {
            return AssetResult<AssetId>(invalid("Animation preview mesh settings are invalid."));
        }
        return AssetResult<AssetId>(mesh);
    }

    AssetStatus set_animation_preview_mesh_preference(FileSystem& files, const AssetId& animation, const AssetId& mesh)
    {
        if (!animation.valid())
        {
            return invalid("A preview mesh preference requires a valid animation identity.");
        }
        const auto path = VirtualPath::parse("/Saved/Editor/AnimationPreviewMeshes/" + animation.hex() + ".settings");
        if (!mesh.valid())
        {
            const auto removed = files.remove_file(path.value());
            return removed.succeeded() || removed.code == FileErrorCode::NotFound ? AssetStatus::success()
                                                                                  : invalid(removed.message);
        }
        const auto directory = VirtualPath::parse("/Saved/Editor/AnimationPreviewMeshes");
        const auto made = files.create_directories(directory.value());
        if (!made.succeeded())
        {
            return invalid(made.message);
        }
        ValueWriter writer;
        writer.write_uint32(1);
        writer.write_utf8(mesh.hex());
        const auto saved = files.write_binary_atomic(path.value(), writer.bytes(), FilePublishMode::Replace);
        return saved.succeeded() ? AssetStatus::success() : invalid(saved.message);
    }

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

    bool animation_asset_matches_layout(const TypeRegistry& types, const FileSystem& files,
                                        const AssetCatalogEntry& asset, const AnimationBoneLayout& layout)
    {
        if (!animation_asset_uses_skeleton(asset.file, layout.skeleton_id()))
        {
            return false;
        }
        const auto bytes = files.read_binary(asset.path, 1024u * 1024u);
        if (!bytes.succeeded())
        {
            return false;
        }
        const auto description = decode_asset_yaml(types, bytes.value());
        if (!description.succeeded() || !(description.value().index.asset_id == asset.file.asset_id) ||
            description.value().index.root_type != asset.file.root_type)
        {
            return false;
        }
        ValueReader reader(description.value().type_data);
        if (asset.file.root_type == "toy3d.AnimationSequenceAssetData")
        {
            AnimationSequenceAssetData data;
            return decode_value(reader, data).succeeded() && reader.at_end() &&
                   data.skeleton.asset_id == layout.skeleton_id() &&
                   data.skeleton_reference_hash == layout.reference_hash();
        }
        if (asset.file.root_type == "toy3d.SkeletalMeshAssetData")
        {
            SkeletalMeshAssetData data;
            return decode_value(reader, data).succeeded() && reader.at_end() &&
                   data.skeleton.asset_id == layout.skeleton_id() &&
                   data.skeleton_reference_hash == layout.reference_hash();
        }
        return false;
    }

    AssetResult<MeshPreviewAsset> load_mesh_preview_asset(AssetPairStore& pairs, const AssetCatalog& catalog,
                                                          const AssetId& id, bool override_selection,
                                                          const AssetId& selected_mesh,
                                                          const AssetId& selected_sequence,
                                                          std::shared_ptr<const MeshPreviewAsset> reusable,
                                                          const FileSystem* editor_settings)
    {
        MeshPreviewAsset result;
        const auto* location = catalog.index.find(id);
        if (!location)
        {
            return AssetResult<MeshPreviewAsset>(invalid("Preview asset is missing from the catalog."));
        }
        const auto primary = pairs.read(location->path);
        if (!primary.succeeded())
        {
            return AssetResult<MeshPreviewAsset>(primary.status());
        }
        if (!(primary.value().description.index.asset_id == id) ||
            primary.value().description.index.root_type != location->index.root_type)
        {
            return AssetResult<MeshPreviewAsset>(invalid("Preview identity/type changed during load."));
        }
        result.id = id;
        result.path = location->path.utf8();
        result.root_type = location->index.root_type;
        result.sources.push_back({id, location->path, primary.value().description_bytes});
        if (result.root_type == "toy3d.StaticMeshAssetData")
        {
            result.mesh_id = id;
            if (reusable && reusable->id == id && reusable->static_mesh &&
                matches_source(reusable.get(), id, location->path, primary.value()))
            {
                result.static_mesh = reusable->static_mesh;
            }
            else
            {
                auto geometry = decode_static_mesh_asset_pair(primary.value());
                if (!geometry.succeeded())
                {
                    return AssetResult<MeshPreviewAsset>(geometry.status());
                }
                result.static_mesh = std::make_shared<const StaticMeshAssetGeometry>(std::move(geometry).value());
            }
            const auto materials =
                capture_mesh_material_sources(pairs, catalog, result.static_mesh->default_materials, result.sources);
            return materials.succeeded() ? AssetResult<MeshPreviewAsset>(std::move(result))
                                         : AssetResult<MeshPreviewAsset>(materials);
        }
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
                    return AssetResult<MeshPreviewAsset>(mesh.status());
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
                return AssetResult<MeshPreviewAsset>(sequence.status());
            }
            skeleton_id = sequence.value().data.skeleton.asset_id;
            primary_sequence = std::make_shared<const AnimationSequenceAsset>(std::move(sequence).value());
            if (!override_selection)
            {
                result.sequence_id = id;
                if (editor_settings)
                {
                    const auto preferred = animation_preview_mesh_preference(*editor_settings, id);
                    if (!preferred.succeeded())
                    {
                        return AssetResult<MeshPreviewAsset>(preferred.status());
                    }
                    result.mesh_id = preferred.value();
                    result.uses_preview_preference = true;
                    result.preferred_mesh = preferred.value();
                }
                std::vector<const AssetCatalogEntry*> candidates;
                for (const auto& entry : catalog.entries)
                {
                    if (entry.file.root_type == "toy3d.SkeletalMeshAssetData" &&
                        animation_asset_uses_skeleton(entry.file, skeleton_id))
                    {
                        candidates.push_back(&entry);
                    }
                }
                std::sort(candidates.begin(), candidates.end(),
                          [](const AssetCatalogEntry* a, const AssetCatalogEntry* b)
                          {
                              return a->file.asset_id < b->file.asset_id;
                          });
                if (!result.mesh_id.valid())
                {
                    for (const auto* candidate : candidates)
                    {
                        const auto pair = pairs.read(candidate->path);
                        if (!pair.succeeded())
                        {
                            continue;
                        }
                        auto decoded = decode_skeletal_mesh_asset_pair(pair.value());
                        if (decoded.succeeded() && decoded.value().data.skeleton_reference_hash ==
                                                       primary_sequence->data.skeleton_reference_hash)
                        {
                            result.mesh_id = candidate->file.asset_id;
                            break;
                        }
                    }
                }
            }
        }
        else
        {
            return AssetResult<MeshPreviewAsset>(invalid("Unsupported mesh preview asset type."));
        }
        if (override_selection)
        {
            result.mesh_id = selected_mesh;
            result.sequence_id = selected_sequence;
        }
        const auto* skeleton_location = catalog.index.find(skeleton_id);
        if (!skeleton_location || skeleton_location->index.root_type != "toy3d.SkeletonAssetData")
        {
            return AssetResult<MeshPreviewAsset>(invalid("Preview Skeleton is missing."));
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
            return AssetResult<MeshPreviewAsset>(skeleton_pair.status());
        }
        const auto skeleton = decode_skeleton_asset_pair(skeleton_pair.value());
        if (!skeleton.succeeded() || !(skeleton_pair.value().description.index.asset_id == skeleton_id))
        {
            return AssetResult<MeshPreviewAsset>(invalid("Preview Skeleton identity/data is invalid."));
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
            return AssetResult<MeshPreviewAsset>(result.layout->status());
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
            return AssetResult<MeshPreviewAsset>(primary_status);
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
                return AssetResult<MeshPreviewAsset>(invalid("Preview selection was removed."));
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
                return AssetResult<MeshPreviewAsset>(pair.status());
            }
            if (!(pair.value().description.index.asset_id == selected))
            {
                return AssetResult<MeshPreviewAsset>(invalid("Preview selection identity changed."));
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
                        return AssetResult<MeshPreviewAsset>(decoded.status());
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
                        return AssetResult<MeshPreviewAsset>(decoded.status());
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
                return AssetResult<MeshPreviewAsset>(status);
            }
            if (!(selected == id))
            {
                result.sources.push_back({selected, selected_location->path, pair.value().description_bytes});
            }
        }
        if (result.mesh)
        {
            const auto materials =
                capture_mesh_material_sources(pairs, catalog, result.mesh->data.default_materials, result.sources);
            if (!materials.succeeded())
            {
                return AssetResult<MeshPreviewAsset>(materials);
            }
        }
        return AssetResult<MeshPreviewAsset>(std::move(result));
    }

    AssetStatus capture_mesh_material_sources(AssetPairStore& pairs, const AssetCatalog& catalog,
                                              const std::vector<AssetRef>& materials,
                                              std::vector<MeshPreviewAsset::Source>& sources)
    {
        auto pending = materials;
        for (std::size_t i = 0; i < pending.size(); ++i)
        {
            const auto reference = pending[i];
            if (!reference.asset_id.valid() || std::any_of(sources.begin(), sources.end(),
                                                           [&reference](const MeshPreviewAsset::Source& source)
                                                           {
                                                               return source.id == reference.asset_id;
                                                           }))
            {
                continue;
            }
            if (pending.size() > 4096 || sources.size() > 4096)
            {
                return invalid("Mesh material dependency graph exceeds the preview limit.");
            }
            const auto valid = catalog.index.resolve(reference, "mesh default material");
            if (!valid.succeeded())
            {
                return valid;
            }
            const auto* location = catalog.index.find(reference.asset_id);
            const auto pair = pairs.read(location->path);
            if (!pair.succeeded())
            {
                return pair.status();
            }
            if (!(pair.value().description.index.asset_id == reference.asset_id) ||
                pair.value().description.index.root_type != reference.expected_type)
            {
                return invalid("Mesh material dependency changed identity/type.");
            }
            sources.push_back({reference.asset_id, location->path, pair.value().description_bytes});
            const auto& dependencies = pair.value().description.index.dependencies;
            pending.insert(pending.end(), dependencies.begin(), dependencies.end());
        }
        return AssetStatus::success();
    }

    bool mesh_preview_asset_current(AssetPairStore& pairs, const AssetCatalog& catalog, const MeshPreviewAsset& asset,
                                    const FileSystem* editor_settings)
    {
        if (asset.uses_preview_preference && editor_settings)
        {
            const auto preference = animation_preview_mesh_preference(*editor_settings, asset.id);
            if (!preference.succeeded() || !(preference.value() == asset.preferred_mesh))
            {
                return false;
            }
        }
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
