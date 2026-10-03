#include "skeletal_mesh_asset.h"

#include <algorithm>
#include <set>

#include "serialization/math_value_codec.h"

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, "skeletal_geometry", {}, message, {}};
        }

        bool contains(const BoneLocalBounds& bounds, const Vector3& point)
        {
            return point.x >= bounds.minimum.x - skin_bind_tolerance &&
                   point.x <= bounds.maximum.x + skin_bind_tolerance &&
                   point.y >= bounds.minimum.y - skin_bind_tolerance &&
                   point.y <= bounds.maximum.y + skin_bind_tolerance &&
                   point.z >= bounds.minimum.z - skin_bind_tolerance &&
                   point.z <= bounds.maximum.z + skin_bind_tolerance;
        }
    } // namespace

    AssetStatus validate_skeletal_mesh_geometry(const SkeletalMeshAssetGeometry& geometry)
    {
        const auto valid = validate_static_mesh_geometry(geometry.mesh);
        if (!valid.succeeded())
        {
            return valid;
        }
        if (geometry.num_bone_influences != skin_influences_per_group &&
            geometry.num_bone_influences != max_skin_influences)
        {
            return invalid("skin influence storage must have four or eight slots");
        }
        for (const auto& skin : geometry.skin_weights)
        {
            for (std::size_t j = geometry.num_bone_influences; j < max_skin_influences; ++j)
            {
                if (skin.weights[j] != 0 || skin.bone_indices[j] != 0)
                {
                    return invalid("inactive skin storage slots must be zero");
                }
            }
        }
        const std::size_t bone_count = geometry.inverse_bind_matrices.size();
        if (bone_count == 0 || bone_count > max_skeleton_bones || geometry.bone_local_bounds.size() != bone_count ||
            geometry.skin_weights.size() != geometry.mesh.vertices.size() ||
            geometry.section_bone_maps.size() != geometry.mesh.sections.size())
        {
            return invalid("skeletal geometry array sizes disagree");
        }
        std::vector<bool> influenced(bone_count, false);
        for (std::size_t i = 0; i < bone_count; ++i)
        {
            Matrix4 inverse;
            const auto& matrix = geometry.inverse_bind_matrices[i];
            const auto& bounds = geometry.bone_local_bounds[i];
            if (!is_finite(matrix) || !try_inverse(matrix, inverse))
            {
                return invalid("inverse bind matrix is nonfinite or singular");
            }
            if (matrix.at(0, 3) != 0 || matrix.at(1, 3) != 0 || matrix.at(2, 3) != 0 || matrix.at(3, 3) != 1)
            {
                auto status = invalid("inverse bind matrix must be affine");
                status.message += " at bone " + std::to_string(i) + " row=" + std::to_string(matrix.at(0, 3)) + "," +
                                  std::to_string(matrix.at(1, 3)) + "," + std::to_string(matrix.at(2, 3)) + "," +
                                  std::to_string(matrix.at(3, 3));
                return status;
            }
            if (!is_finite(bounds.minimum) || !is_finite(bounds.maximum) || bounds.minimum.x > bounds.maximum.x ||
                bounds.minimum.y > bounds.maximum.y || bounds.minimum.z > bounds.maximum.z)
            {
                return invalid("invalid bone bounds");
            }
        }
        for (std::size_t section_index = 0; section_index < geometry.mesh.sections.size(); ++section_index)
        {
            const auto& section = geometry.mesh.sections[section_index];
            const auto& bone_map = geometry.section_bone_maps[section_index];
            std::set<std::uint32_t> unique;
            if (bone_map.empty() || bone_map.size() > max_section_bones)
            {
                return invalid("section bone count is outside the draw budget");
            }
            for (const auto bone : bone_map)
            {
                if (bone >= bone_count || !unique.insert(bone).second)
                {
                    return invalid("invalid or duplicate section bone mapping");
                }
            }
            for (std::size_t i = section.first_index; i < section.first_index + section.index_count; ++i)
            {
                const auto vertex_index = geometry.mesh.indices[i];
                const auto& weights = geometry.skin_weights[vertex_index];
                std::uint32_t sum = 0;
                for (std::size_t j = 0; j < geometry.num_bone_influences; ++j)
                {
                    sum += weights.weights[j];
                    // Every fetched index must be valid, including zero-weight entries.
                    if (weights.bone_indices[j] >= bone_map.size())
                    {
                        return invalid("out of range section-local bone index");
                    }
                    if (weights.weights[j] != 0)
                    {
                        const auto bone = bone_map[weights.bone_indices[j]];
                        influenced[bone] = true;
                        const auto point = transform_position(geometry.inverse_bind_matrices[bone],
                                                              geometry.mesh.vertices[vertex_index].position);
                        if (!geometry.bone_local_bounds[bone].influenced ||
                            !contains(geometry.bone_local_bounds[bone], point))
                        {
                            return invalid("bone-local bounds do not contain influenced vertices");
                        }
                    }
                }
                if (sum != 255)
                {
                    return invalid("quantized skin weights must sum to 255");
                }
            }
        }
        for (std::size_t i = 0; i < bone_count; ++i)
        {
            if (geometry.bone_local_bounds[i].influenced != influenced[i])
            {
                return invalid("bone bounds influence mask disagrees with mesh");
            }
        }
        return AssetStatus::success();
    }

    AssetStatus validate_skeletal_mesh(const SkeletalMeshAsset& mesh)
    {
        if (!validate_skeleton_reference(mesh.data.skeleton, mesh.data.skeleton_reference_hash).succeeded() ||
            mesh.data.material_slots != mesh.geometry.mesh.material_slots ||
            mesh.data.vertex_count != mesh.geometry.mesh.vertices.size() ||
            mesh.data.index_count != mesh.geometry.mesh.indices.size() ||
            mesh.data.geometry_segment != "skeletal_geometry")
        {
            return invalid("skeletal mesh metadata disagrees with geometry");
        }
        return validate_skeletal_mesh_geometry(mesh.geometry);
    }

    AssetStatus validate_skeletal_mesh_compatibility(const SkeletalMeshAsset& mesh, const AssetId& skeleton_id,
                                                     const SkeletonAssetData& skeleton)
    {
        const auto valid = validate_skeletal_mesh(mesh);
        if (!valid.succeeded())
        {
            return valid;
        }
        const auto hash = skeleton_reference_hash(skeleton);
        if (!hash.succeeded())
        {
            return hash.status();
        }
        if (!(mesh.data.skeleton.asset_id == skeleton_id) || mesh.data.skeleton_reference_hash != hash.value() ||
            mesh.geometry.inverse_bind_matrices.size() != skeleton.bones.size())
        {
            return invalid("skeletal mesh skeleton identity or reference contract mismatch");
        }
        std::vector<Matrix4> component_pose(skeleton.bones.size());
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
        {
            const auto& bone = skeleton.bones[i];
            const Matrix4 local = to_matrix(bone.reference_local_transform);
            component_pose[i] = bone.parent_index < 0 ? local : component_pose[bone.parent_index] * local;
            if (!is_nearly_equal(component_pose[i] * mesh.geometry.inverse_bind_matrices[i], Matrix4::identity(),
                                 skin_bind_tolerance))
            {
                return invalid("mesh bind pose does not produce identity skin matrices");
            }
        }
        return AssetStatus::success();
    }

    AssetResult<std::vector<std::uint8_t>> encode_skeletal_mesh_geometry(const SkeletalMeshAssetGeometry& geometry)
    {
        const auto valid = validate_skeletal_mesh_geometry(geometry);
        if (!valid.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(valid);
        }
        const auto mesh = encode_static_mesh_geometry(geometry.mesh);
        if (!mesh.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(mesh.status());
        }
        ValueWriter writer;
        if (!writer.write_uint32(3).succeeded() || !writer.write_uint32(geometry.num_bone_influences).succeeded() ||
            !writer.write_blob(mesh.value()).succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(invalid("skeletal geometry encoding failed"));
        }
        for (const auto& skin : geometry.skin_weights)
        {
            for (std::size_t i = 0; i < geometry.num_bone_influences; ++i)
            {
                if (!writer.write_uint8(skin.bone_indices[i]).succeeded())
                {
                    return AssetResult<std::vector<std::uint8_t>>(invalid("skin index encoding failed"));
                }
            }
            for (std::size_t i = 0; i < geometry.num_bone_influences; ++i)
            {
                if (!writer.write_uint8(skin.weights[i]).succeeded())
                {
                    return AssetResult<std::vector<std::uint8_t>>(invalid("skin weight encoding failed"));
                }
            }
        }
        for (const auto& bone_map : geometry.section_bone_maps)
        {
            if (!writer.write_array_length(static_cast<std::uint32_t>(bone_map.size())).succeeded())
            {
                return AssetResult<std::vector<std::uint8_t>>(invalid("bone map encoding failed"));
            }
            for (const auto bone : bone_map)
            {
                if (!writer.write_uint32(bone).succeeded())
                {
                    return AssetResult<std::vector<std::uint8_t>>(invalid("bone map encoding failed"));
                }
            }
        }
        if (!writer.write_array_length(static_cast<std::uint32_t>(geometry.inverse_bind_matrices.size())).succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(invalid("bind count encoding failed"));
        }
        for (std::size_t i = 0; i < geometry.inverse_bind_matrices.size(); ++i)
        {
            const auto& bounds = geometry.bone_local_bounds[i];
            if (!encode_value(writer, geometry.inverse_bind_matrices[i]).succeeded() ||
                !writer.write_bool(bounds.influenced).succeeded() ||
                !encode_value(writer, bounds.minimum).succeeded() || !encode_value(writer, bounds.maximum).succeeded())
            {
                return AssetResult<std::vector<std::uint8_t>>(invalid("bind or bounds encoding failed"));
            }
        }
        return AssetResult<std::vector<std::uint8_t>>(writer.bytes());
    }

    AssetResult<SkeletalMeshAssetGeometry> decode_skeletal_mesh_geometry(const std::vector<std::uint8_t>& bytes)
    {
        ValueReader reader(bytes);
        std::uint32_t version = 0;
        std::uint32_t num_bone_influences = skin_influences_per_group;
        std::vector<std::uint8_t> mesh_bytes;
        if (bytes.size() > ValueLimits{}.max_bytes || !reader.read_uint32(version).succeeded() || version != 3 ||
            !reader.read_uint32(num_bone_influences).succeeded() ||
            (num_bone_influences != skin_influences_per_group && num_bone_influences != max_skin_influences) ||
            !reader.read_blob(mesh_bytes).succeeded())
        {
            return AssetResult<SkeletalMeshAssetGeometry>(invalid("invalid skeletal geometry header"));
        }
        const auto mesh = decode_static_mesh_geometry(mesh_bytes);
        if (!mesh.succeeded())
        {
            return AssetResult<SkeletalMeshAssetGeometry>(mesh.status());
        }
        SkeletalMeshAssetGeometry geometry;
        geometry.mesh = mesh.value();
        geometry.num_bone_influences = num_bone_influences;
        if (geometry.mesh.vertices.size() > (bytes.size() - reader.offset()) / (num_bone_influences * 2u))
        {
            return AssetResult<SkeletalMeshAssetGeometry>(invalid("truncated skin storage"));
        }
        geometry.skin_weights.resize(geometry.mesh.vertices.size());
        for (auto& skin : geometry.skin_weights)
        {
            for (std::size_t i = 0; i < num_bone_influences; ++i)
            {
                if (!reader.read_uint8(skin.bone_indices[i]).succeeded())
                {
                    return AssetResult<SkeletalMeshAssetGeometry>(invalid("truncated skin indices"));
                }
            }
            for (std::size_t i = 0; i < num_bone_influences; ++i)
            {
                if (!reader.read_uint8(skin.weights[i]).succeeded())
                {
                    return AssetResult<SkeletalMeshAssetGeometry>(invalid("truncated skin weights"));
                }
            }
        }
        geometry.section_bone_maps.resize(geometry.mesh.sections.size());
        std::uint32_t count = 0;
        for (auto& bone_map : geometry.section_bone_maps)
        {
            if (!reader.read_array_length(count).succeeded() || count == 0 || count > max_section_bones ||
                count > (bytes.size() - reader.offset()) / sizeof(std::uint32_t))
            {
                return AssetResult<SkeletalMeshAssetGeometry>(invalid("invalid bone map count"));
            }
            bone_map.resize(count);
            for (auto& bone : bone_map)
            {
                if (!reader.read_uint32(bone).succeeded())
                {
                    return AssetResult<SkeletalMeshAssetGeometry>(invalid("truncated bone map"));
                }
            }
        }
        if (!reader.read_array_length(count).succeeded() || count == 0 || count > max_skeleton_bones ||
            count > (bytes.size() - reader.offset()) / 89)
        {
            return AssetResult<SkeletalMeshAssetGeometry>(invalid("invalid bind count"));
        }
        geometry.inverse_bind_matrices.resize(count);
        geometry.bone_local_bounds.resize(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            auto& bounds = geometry.bone_local_bounds[i];
            if (!decode_value(reader, geometry.inverse_bind_matrices[i]).succeeded() ||
                !reader.read_bool(bounds.influenced).succeeded() || !decode_value(reader, bounds.minimum).succeeded() ||
                !decode_value(reader, bounds.maximum).succeeded())
            {
                return AssetResult<SkeletalMeshAssetGeometry>(invalid("truncated bind or bounds"));
            }
        }
        if (!reader.at_end())
        {
            return AssetResult<SkeletalMeshAssetGeometry>(invalid("trailing skeletal geometry data"));
        }
        const auto valid = validate_skeletal_mesh_geometry(geometry);
        return valid.succeeded() ? AssetResult<SkeletalMeshAssetGeometry>(std::move(geometry))
                                 : AssetResult<SkeletalMeshAssetGeometry>(valid);
    }

    AssetResult<AssetPairBytes> encode_skeletal_mesh_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                                const SkeletalMeshAsset& mesh)
    {
        const auto valid = validate_skeletal_mesh(mesh);
        if (!valid.succeeded())
        {
            return AssetResult<AssetPairBytes>(valid);
        }
        const auto geometry = encode_skeletal_mesh_geometry(mesh.geometry);
        if (!geometry.succeeded())
        {
            return AssetResult<AssetPairBytes>(geometry.status());
        }
        ValueWriter writer;
        if (!encode_value(writer, mesh.data).succeeded())
        {
            return AssetResult<AssetPairBytes>(invalid("skeletal mesh metadata encoding failed"));
        }
        AssetFileIndex index;
        index.asset_id = id;
        index.root_type = "toy3d.SkeletalMeshAssetData";
        index.schema_version = 2;
        index.dependencies.push_back(mesh.data.skeleton);
        return encode_asset_pair(types, std::move(index), writer.bytes(),
                                 {{"skeletal_geometry", 2, true, geometry.value()}});
    }

    AssetResult<SkeletalMeshAsset> decode_skeletal_mesh_asset_pair(const AssetPair& pair)
    {
        if (pair.description.index.root_type != "toy3d.SkeletalMeshAssetData" ||
            pair.description.index.schema_version != 2 || !pair.description.has_meta)
        {
            return AssetResult<SkeletalMeshAsset>(invalid("invalid skeletal mesh type or schema"));
        }
        ValueReader reader(pair.description.type_data);
        SkeletalMeshAsset mesh;
        if (!decode_value(reader, mesh.data).succeeded() || !reader.at_end())
        {
            return AssetResult<SkeletalMeshAsset>(invalid("invalid skeletal mesh metadata"));
        }
        const AssetSegmentData* geometry = nullptr;
        for (const auto& segment : pair.meta.segments)
        {
            if (segment.name == "skeletal_geometry" && segment.kind == 2 && segment.required)
            {
                geometry = &segment;
            }
            else if (segment.required)
            {
                return AssetResult<SkeletalMeshAsset>(invalid("unknown required skeletal mesh segment"));
            }
        }
        if (!geometry)
        {
            return AssetResult<SkeletalMeshAsset>(invalid("missing skeletal geometry"));
        }
        const auto decoded = decode_skeletal_mesh_geometry(geometry->bytes);
        if (!decoded.succeeded())
        {
            return AssetResult<SkeletalMeshAsset>(decoded.status());
        }
        mesh.geometry = decoded.value();
        const auto valid = validate_skeletal_mesh(mesh);
        return valid.succeeded() ? AssetResult<SkeletalMeshAsset>(std::move(mesh))
                                 : AssetResult<SkeletalMeshAsset>(valid);
    }
} // namespace toy3d
