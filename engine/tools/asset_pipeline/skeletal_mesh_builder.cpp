#include "skeletal_mesh_builder.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <set>

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }

        struct QuantizedInfluences
        {
            std::array<std::uint32_t, max_skin_influences> bones{};
            std::array<std::uint8_t, max_skin_influences> weights{};
        };

        AssetStatus quantize(const std::vector<BoneInfluence>& source, std::size_t bone_count,
                             const SkeletalMeshBuildOptions& options, QuantizedInfluences& output,
                             SkeletalMeshBuildResult& result)
        {
            std::map<std::uint32_t, double> merged;
            if (source.size() > max_skeleton_bones)
            {
                return invalid("source vertex influence count exceeds budget");
            }
            for (const auto& influence : source)
            {
                if (influence.bone_index >= bone_count || !is_finite(influence.weight) || influence.weight < 0)
                {
                    return invalid("invalid skin influence");
                }
                if (influence.weight > 0)
                {
                    merged[influence.bone_index] += influence.weight;
                }
            }
            std::vector<BoneInfluence> influences;
            double total = 0.0;
            for (const auto& item : merged)
            {
                if (!std::isfinite(item.second) || item.second > std::numeric_limits<float>::max())
                {
                    return invalid("merged skin influence overflow");
                }
                total += item.second;
                influences.push_back({item.first, static_cast<float>(item.second)});
            }
            if (total <= 0 || !std::isfinite(total))
            {
                return invalid("vertex has no positive skin weight");
            }
            std::stable_sort(influences.begin(), influences.end(),
                             [](const BoneInfluence& a, const BoneInfluence& b)
                             {
                                 return a.weight > b.weight;
                             });
            if (influences.size() > max_skin_influences)
            {
                if (!options.allow_reduce_influences)
                {
                    return invalid("vertex has more than eight influences; explicit reduction is required");
                }
                double discarded = 0.0;
                for (std::size_t i = max_skin_influences; i < influences.size(); ++i)
                {
                    discarded += influences[i].weight;
                }
                ++result.reduced_vertex_count;
                result.maximum_discarded_weight =
                    std::max(result.maximum_discarded_weight, static_cast<float>(discarded / total));
                influences.resize(max_skin_influences);
            }
            total = 0.0;
            for (const auto& influence : influences)
            {
                total += influence.weight;
            }
            std::array<double, max_skin_influences> remainder{};
            std::uint32_t sum = 0;
            for (std::size_t i = 0; i < influences.size(); ++i)
            {
                const double scaled = influences[i].weight / total * 255.0;
                output.bones[i] = influences[i].bone_index;
                output.weights[i] = static_cast<std::uint8_t>(std::floor(scaled));
                remainder[i] = scaled - output.weights[i];
                sum += output.weights[i];
            }
            while (sum < 255)
            {
                const auto index =
                    static_cast<std::size_t>(std::max_element(remainder.begin(), remainder.end()) - remainder.begin());
                ++output.weights[index];
                remainder[index] = -1.0;
                ++sum;
            }
            std::size_t packed = 0;
            for (std::size_t i = 0; i < max_skin_influences; ++i)
            {
                if (output.weights[i] != 0)
                {
                    output.bones[packed] = output.bones[i];
                    output.weights[packed] = output.weights[i];
                    ++packed;
                }
            }
            for (std::size_t i = packed; i < max_skin_influences; ++i)
            {
                output.bones[i] = 0;
                output.weights[i] = 0;
            }
            return AssetStatus::success();
        }

        void extend(BoneLocalBounds& bounds, const Vector3& point)
        {
            if (!bounds.influenced)
            {
                bounds.influenced = true;
                bounds.minimum = point;
                bounds.maximum = point;
            }
            else
            {
                bounds.minimum = Vector3(std::min(bounds.minimum.x, point.x), std::min(bounds.minimum.y, point.y),
                                         std::min(bounds.minimum.z, point.z));
                bounds.maximum = Vector3(std::max(bounds.maximum.x, point.x), std::max(bounds.maximum.y, point.y),
                                         std::max(bounds.maximum.z, point.z));
            }
        }
    } // namespace

    AssetResult<SkeletalMeshBuildResult> build_skeletal_mesh(const SkeletalMeshBuildInput& input,
                                                             const AssetId& skeleton_id,
                                                             const SkeletonAssetData& skeleton,
                                                             const SkeletalMeshBuildOptions& options)
    {
        const auto source_valid = validate_static_mesh_geometry(input.mesh);
        const auto hash = skeleton_reference_hash(skeleton);
        if (!source_valid.succeeded())
        {
            return AssetResult<SkeletalMeshBuildResult>(source_valid);
        }
        if (!hash.succeeded() || !skeleton_id.valid() || input.influences.size() != input.mesh.vertices.size() ||
            input.inverse_bind_matrices.size() != skeleton.bones.size())
        {
            return AssetResult<SkeletalMeshBuildResult>(invalid("invalid skeletal build input"));
        }
        SkeletalMeshBuildResult result;
        auto& output = result.mesh.geometry;
        output.mesh.material_slots = input.mesh.material_slots;
        output.inverse_bind_matrices = input.inverse_bind_matrices;
        output.bone_local_bounds.resize(skeleton.bones.size());
        std::vector<QuantizedInfluences> quantized(input.mesh.vertices.size());
        for (std::size_t i = 0; i < input.influences.size(); ++i)
        {
            const auto valid = quantize(input.influences[i], skeleton.bones.size(), options, quantized[i], result);
            if (!valid.succeeded())
            {
                return AssetResult<SkeletalMeshBuildResult>(valid);
            }
        }
        // Split in stable source triangle order. Each output section owns its vertices;
        // local bone indices cannot leak between two different section mappings.
        for (const auto& source_section : input.mesh.sections)
        {
            std::map<std::uint32_t, std::uint32_t> vertex_map;
            std::map<std::uint32_t, std::uint8_t> bone_map;
            auto start_section = [&]()
            {
                output.mesh.sections.push_back(
                    {static_cast<std::uint32_t>(output.mesh.indices.size()), 0, source_section.material_slot});
                output.section_bone_maps.emplace_back();
                vertex_map.clear();
                bone_map.clear();
            };
            start_section();
            for (std::size_t triangle = source_section.first_index;
                 triangle < source_section.first_index + source_section.index_count; triangle += 3)
            {
                std::set<std::uint32_t> triangle_bones;
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const auto& skin = quantized[input.mesh.indices[triangle + corner]];
                    for (std::size_t influence = 0; influence < max_skin_influences; ++influence)
                    {
                        if (skin.weights[influence] != 0)
                        {
                            triangle_bones.insert(skin.bones[influence]);
                        }
                    }
                }
                std::size_t added = 0;
                for (const auto bone : triangle_bones)
                {
                    added += bone_map.count(bone) == 0 ? 1 : 0;
                }
                if (bone_map.size() + added > max_section_bones)
                {
                    start_section();
                }
                for (const auto bone : triangle_bones)
                {
                    if (bone_map.count(bone) == 0)
                    {
                        bone_map.emplace(bone, static_cast<std::uint8_t>(bone_map.size()));
                        output.section_bone_maps.back().push_back(bone);
                    }
                }
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const auto source_vertex = input.mesh.indices[triangle + corner];
                    auto found = vertex_map.find(source_vertex);
                    if (found == vertex_map.end())
                    {
                        if (output.mesh.vertices.size() >= 1000000)
                        {
                            return AssetResult<SkeletalMeshBuildResult>(
                                invalid("section splitting exceeds vertex budget"));
                        }
                        const auto new_index = static_cast<std::uint32_t>(output.mesh.vertices.size());
                        found = vertex_map.emplace(source_vertex, new_index).first;
                        output.mesh.vertices.push_back(input.mesh.vertices[source_vertex]);
                        SkinWeights skin;
                        const auto& source_skin = quantized[source_vertex];
                        skin.weights = source_skin.weights;
                        for (std::size_t i = 0; i < max_skin_influences; ++i)
                        {
                            if (skin.weights[i] != 0)
                            {
                                const auto bone = source_skin.bones[i];
                                skin.bone_indices[i] = bone_map.at(bone);
                                extend(output.bone_local_bounds[bone],
                                       transform_position(input.inverse_bind_matrices[bone],
                                                          input.mesh.vertices[source_vertex].position));
                            }
                        }
                        for (std::size_t i = skin_influences_per_group; i < max_skin_influences; ++i)
                        {
                            if (skin.weights[i] != 0)
                            {
                                output.num_bone_influences = max_skin_influences;
                            }
                        }
                        output.skin_weights.push_back(skin);
                    }
                    output.mesh.indices.push_back(found->second);
                    ++output.mesh.sections.back().index_count;
                }
            }
        }
        auto& data = result.mesh.data;
        data.skeleton = {skeleton_id, {}, "toy3d.SkeletonAssetData", AssetRefStrength::Strong};
        data.skeleton_reference_hash = hash.value();
        data.material_slots = output.mesh.material_slots;
        data.vertex_count = static_cast<std::uint32_t>(output.mesh.vertices.size());
        data.index_count = static_cast<std::uint32_t>(output.mesh.indices.size());
        const auto valid = validate_skeletal_mesh_compatibility(result.mesh, skeleton_id, skeleton);
        return valid.succeeded() ? AssetResult<SkeletalMeshBuildResult>(std::move(result))
                                 : AssetResult<SkeletalMeshBuildResult>(valid);
    }
} // namespace toy3d
