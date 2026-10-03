#include "mesh_material_edit_session.h"

#include <algorithm>

#include "asset/mesh/mesh_materials.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const std::string& message)
        {
            return {AssetErrorCode::InvalidState, {}, {}, {}, {}, message, {}};
        }
    } // namespace

    // --------------------------------------------------------------------------
    // MeshMaterialEditSession: shared mesh defaults, history and conflict-checked pair save
    // --------------------------------------------------------------------------
    MeshMaterialEditSession::MeshMaterialEditSession(EditorWorkspace& workspace) : workspace_(workspace)
    {
    }

    AssetStatus MeshMaterialEditSession::open(const AssetId& id, Prepare prepare)
    {
        const auto* location = workspace_.catalog().index.find(id);
        if (!location)
        {
            return invalid("Mesh asset is missing.");
        }
        const auto read = workspace_.asset_pairs().read(location->path);
        if (!read.succeeded())
        {
            return read.status();
        }
        const auto& pair = read.value();
        if (!(pair.description.index.asset_id == id))
        {
            return invalid("Mesh identity changed while opening.");
        }
        clear();
        id_ = id;
        path_ = location->path;
        opened_ = pair;
        ValueReader reader(pair.description.type_data);
        const auto* type = workspace_.types().find(pair.description.index.root_type);
        if (pair.description.index.root_type == "toy3d.StaticMeshAssetData")
        {
            const auto geometry = decode_static_mesh_asset_pair(pair);
            StaticMeshAssetData data;
            if (!geometry.succeeded() || !decode_value(reader, data).succeeded() || !reader.at_end())
            {
                clear();
                return invalid("Invalid StaticMesh material edit input.");
            }
            static_ = std::make_unique<EditSession<StaticMeshAssetData>>(
                workspace_.types(), *type, id, path_, data,
                [](const StaticMeshAssetData& candidate)
                {
                    return validate_mesh_materials(candidate.material_slots, candidate.default_materials);
                },
                &workspace_.catalog().index,
                [prepare](const StaticMeshAssetData& candidate, EditChangeKind)
                {
                    return prepare(candidate.default_materials);
                });
        }
        else if (pair.description.index.root_type == "toy3d.SkeletalMeshAssetData")
        {
            const auto mesh = decode_skeletal_mesh_asset_pair(pair);
            if (!mesh.succeeded())
            {
                clear();
                return mesh.status();
            }
            skeletal_ = std::make_unique<EditSession<SkeletalMeshAssetData>>(
                workspace_.types(), *type, id, path_, mesh.value().data,
                [](const SkeletalMeshAssetData& candidate)
                {
                    return validate_mesh_materials(candidate.material_slots, candidate.default_materials);
                },
                &workspace_.catalog().index,
                [prepare](const SkeletalMeshAssetData& candidate, EditChangeKind)
                {
                    return prepare(candidate.default_materials);
                });
        }
        else
        {
            clear();
            return invalid("Default materials belong to a StaticMesh or SkeletalMesh asset.");
        }
        saved_materials_ = materials();
        return AssetStatus::success();
    }

    void MeshMaterialEditSession::clear()
    {
        static_.reset();
        skeletal_.reset();
        opened_ = {};
        saved_materials_.clear();
        path_ = {};
        id_ = {};
    }
    bool MeshMaterialEditSession::active() const
    {
        return static_ || skeletal_;
    }
    bool MeshMaterialEditSession::writable() const
    {
        return active() && path_.utf8().compare(0, 9, "/Project/") == 0;
    }
    bool MeshMaterialEditSession::dirty() const
    {
        return static_ ? static_->dirty() : skeletal_ && skeletal_->dirty();
    }
    const AssetId& MeshMaterialEditSession::id() const
    {
        return id_;
    }
    const std::vector<std::string>& MeshMaterialEditSession::slots() const
    {
        static const std::vector<std::string> empty;
        return static_ ? static_->value().material_slots : skeletal_ ? skeletal_->value().material_slots : empty;
    }
    const std::vector<AssetRef>& MeshMaterialEditSession::materials() const
    {
        static const std::vector<AssetRef> empty;
        return static_ ? static_->value().default_materials : skeletal_ ? skeletal_->value().default_materials : empty;
    }
    AssetStatus MeshMaterialEditSession::set_material(std::size_t slot, const AssetRef& material)
    {
        if (!writable() || slot >= slots().size())
        {
            return invalid("Mesh material slot is read-only or missing.");
        }
        auto candidate = materials();
        candidate.resize(slots().size());
        candidate[slot] = material;
        ValueWriter writer;
        const auto count = writer.write_array_length(static_cast<std::uint32_t>(candidate.size()));
        if (!count.succeeded())
        {
            return invalid(count.message);
        }
        for (const auto& reference : candidate)
        {
            const auto encoded = encode_value(writer, reference);
            if (!encoded.succeeded())
            {
                return invalid(encoded.message);
            }
        }
        const EditPatch patch{{PropertyPathPart::field("default_materials")}, writer.bytes(), EditChangeKind::Setter};
        const auto edited = static_ ? static_->apply_edit({patch}) : skeletal_->apply_edit({patch});
        return edited.succeeded() ? AssetStatus::success() : edited.status();
    }
    AssetStatus MeshMaterialEditSession::undo()
    {
        return static_ ? static_->undo() : skeletal_ ? skeletal_->undo() : invalid("No mesh draft.");
    }
    AssetStatus MeshMaterialEditSession::redo()
    {
        return static_ ? static_->redo() : skeletal_ ? skeletal_->redo() : invalid("No mesh draft.");
    }
    AssetStatus MeshMaterialEditSession::save()
    {
        if (!writable())
        {
            return invalid("Mesh asset is read-only.");
        }
        const auto current = workspace_.asset_pairs().read(path_);
        if (!current.succeeded())
        {
            return current.status();
        }
        if (current.value().description_bytes != opened_.description_bytes)
        {
            return {AssetErrorCode::Conflict, id_, path_.utf8(), {}, {}, "Mesh changed on disk; draft retained.", {}};
        }
        ValueWriter writer;
        const auto encoded =
            static_ ? encode_value(writer, static_->value()) : encode_value(writer, skeletal_->value());
        if (!encoded.succeeded())
        {
            return invalid(encoded.message);
        }
        auto index = opened_.description.index;
        // Preserve unrelated declared dependencies, removing only old default material edges.
        index.dependencies.erase(std::remove_if(index.dependencies.begin(), index.dependencies.end(),
                                                [this](const AssetRef& reference)
                                                {
                                                    return std::any_of(
                                                        saved_materials_.begin(), saved_materials_.end(),
                                                        [&reference](const AssetRef& saved)
                                                        {
                                                            return saved.asset_id.valid() &&
                                                                   saved.asset_id == reference.asset_id &&
                                                                   saved.expected_type == reference.expected_type &&
                                                                   saved.strength == reference.strength &&
                                                                   saved.subresource_id == reference.subresource_id;
                                                        });
                                                }),
                                 index.dependencies.end());
        index.dependencies = mesh_material_dependencies(materials(), std::move(index.dependencies));
        const auto pair = encode_asset_pair(workspace_.types(), index, writer.bytes(), opened_.meta.segments);
        if (!pair.succeeded())
        {
            return pair.status();
        }
        const auto published = workspace_.asset_pairs().publish(path_, pair.value(), FilePublishMode::Replace);
        if (!published.succeeded())
        {
            return published;
        }
        const auto marked =
            static_ ? static_->mark_pair_saved(writer.bytes()) : skeletal_->mark_pair_saved(writer.bytes());
        opened_.description_bytes = pair.value().asset;
        opened_.description.index = index;
        opened_.description.type_data = writer.bytes();
        saved_materials_ = materials();
        if (!marked.succeeded())
        {
            return marked;
        }
        return workspace_.refresh() ? AssetStatus::success()
                                    : invalid("Mesh was saved, but catalog refresh failed: " + workspace_.error());
    }
} // namespace toy3d
