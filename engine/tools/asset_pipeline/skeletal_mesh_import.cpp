#include "skeletal_mesh_import.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "asset_pipeline/assimp_import_support.h"

namespace toy3d
{
    namespace
    {
        constexpr std::size_t max_import_nodes = 100000;
        constexpr std::size_t max_import_depth = 64;
        constexpr std::size_t max_import_animations = 64;
        constexpr std::size_t max_import_vertices = 1000000;

        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }

        struct SourceNode
        {
            const aiNode* node = nullptr;
            Matrix4 world;
        };

        bool collect_nodes(const aiNode& node, const Matrix4& parent, std::map<std::string, SourceNode>& nodes,
                           std::vector<const aiNode*>& ordered, std::size_t depth)
        {
            const std::string name = node.mName.C_Str();
            if (depth > max_import_depth || nodes.size() >= max_import_nodes || name.empty() || nodes.count(name))
            {
                return false;
            }
            const auto world = parent * assimp_import::matrix_from_assimp(node.mTransformation);
            if (!is_finite(world))
            {
                return false;
            }
            nodes.emplace(name, SourceNode{&node, world});
            ordered.push_back(&node);
            for (unsigned i = 0; i < node.mNumChildren; ++i)
            {
                if (!node.mChildren[i] || !collect_nodes(*node.mChildren[i], world, nodes, ordered, depth + 1))
                {
                    return false;
                }
            }
            return true;
        }

        bool add_ancestors(const std::string& name, const std::map<std::string, SourceNode>& nodes,
                           std::set<const aiNode*>& required)
        {
            const auto found = nodes.find(name);
            if (found == nodes.end())
            {
                return false;
            }
            const aiNode* node = found->second.node;
            while (node)
            {
                required.insert(node);
                node = node->mParent;
            }
            return true;
        }

        bool vector_keys_valid(const aiVectorKey* keys, unsigned count, double duration)
        {
            if (count > max_animation_samples || (count != 0 && !keys))
            {
                return false;
            }
            for (unsigned i = 0; i < count; ++i)
            {
                if (keys[i].mInterpolation != aiAnimInterpolation_Linear || !std::isfinite(keys[i].mTime) ||
                    keys[i].mTime < 0 || keys[i].mTime > duration || (i != 0 && keys[i].mTime <= keys[i - 1].mTime) ||
                    !is_finite(Vector3(keys[i].mValue.x, keys[i].mValue.y, keys[i].mValue.z)))
                {
                    return false;
                }
            }
            return true;
        }

        bool rotation_keys_valid(const aiQuatKey* keys, unsigned count, double duration)
        {
            if (count > max_animation_samples || (count != 0 && !keys))
            {
                return false;
            }
            for (unsigned i = 0; i < count; ++i)
            {
                const auto& q = keys[i].mValue;
                Quaternion normalized;
                if (keys[i].mInterpolation != aiAnimInterpolation_Linear || !std::isfinite(keys[i].mTime) ||
                    keys[i].mTime < 0 || keys[i].mTime > duration || (i != 0 && keys[i].mTime <= keys[i - 1].mTime) ||
                    !try_normalize(Quaternion(q.x, q.y, q.z, q.w), normalized))
                {
                    return false;
                }
            }
            return true;
        }

        Vector3 sample_vector(const aiVectorKey* keys, unsigned count, double time, const Vector3& fallback,
                              aiAnimBehaviour pre, aiAnimBehaviour post)
        {
            if (count == 0 || (time < keys[0].mTime && pre == aiAnimBehaviour_DEFAULT) ||
                (time > keys[count - 1].mTime && post == aiAnimBehaviour_DEFAULT))
            {
                return fallback;
            }
            const auto second = std::upper_bound(keys, keys + count, time,
                                                 [](double value, const aiVectorKey& key)
                                                 {
                                                     return value < key.mTime;
                                                 });
            const auto first = second == keys ? keys : second - 1;
            const auto end = second == keys + count ? first : second;
            const float alpha =
                end->mTime > first->mTime ? static_cast<float>((time - first->mTime) / (end->mTime - first->mTime)) : 0;
            return Vector3(first->mValue.x, first->mValue.y, first->mValue.z) * (1 - alpha) +
                   Vector3(end->mValue.x, end->mValue.y, end->mValue.z) * alpha;
        }

        Quaternion sample_rotation(const aiQuatKey* keys, unsigned count, double time, const Quaternion& fallback,
                                   aiAnimBehaviour pre, aiAnimBehaviour post)
        {
            if (count == 0 || (time < keys[0].mTime && pre == aiAnimBehaviour_DEFAULT) ||
                (time > keys[count - 1].mTime && post == aiAnimBehaviour_DEFAULT))
            {
                return fallback;
            }
            const auto second = std::upper_bound(keys, keys + count, time,
                                                 [](double value, const aiQuatKey& key)
                                                 {
                                                     return value < key.mTime;
                                                 });
            const auto first = second == keys ? keys : second - 1;
            const auto end = second == keys + count ? first : second;
            const float alpha =
                end->mTime > first->mTime ? static_cast<float>((time - first->mTime) / (end->mTime - first->mTime)) : 0;
            const auto& a = first->mValue;
            const auto& b = end->mValue;
            Quaternion result;
            if (!try_slerp(Quaternion(a.x, a.y, a.z, a.w), Quaternion(b.x, b.y, b.z, b.w), alpha, result))
            {
                return Quaternion(0, 0, 0, 0);
            }
            return result;
        }

        bool supported_behaviour(aiAnimBehaviour behaviour)
        {
            return behaviour == aiAnimBehaviour_DEFAULT || behaviour == aiAnimBehaviour_CONSTANT;
        }
    } // namespace

    AssetResult<ImportedSkeletalMesh> import_skeletal_mesh(const FileSystem& files, const VirtualPath& source,
                                                           const AssetId& skeleton_id,
                                                           const SkeletalMeshImportOptions& options)
    {
        using Result = AssetResult<ImportedSkeletalMesh>;
        const auto& path = source.utf8();
        const auto dot = path.find_last_of('.');
        std::string extension = dot == std::string::npos ? "" : path.substr(dot + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        if (!skeleton_id.valid() || (extension != "fbx" && extension != "gltf" && extension != "glb") ||
            (options.sample_rate != 30 && options.sample_rate != 60))
        {
            return Result(invalid("skeletal import requires FBX/glTF/GLB, an asset identity and a 30/60 Hz rate"));
        }
        auto io = std::make_unique<assimp_import::ImportIO>(files, path.substr(0, path.find_last_of('/')));
        auto* io_observer = io.get();
        Assimp::Importer importer;
        importer.SetIOHandler(io.release());
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION, true);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, false);
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals);
        if (!scene || !scene->mRootNode || scene->mNumMeshes == 0 || scene->mNumMeshes > 10000 ||
            scene->mNumAnimations > max_import_animations || !io_observer->failures.empty())
        {
            auto status = invalid("Assimp did not produce a bounded complete skeletal scene");
            status.virtual_path = path;
            status.message += std::string(": ") + importer.GetErrorString();
            return Result(status);
        }
        Matrix4 conversion;
        Matrix4 inverse_conversion;
        if (!assimp_import::source_conversion(*scene, extension == "fbx", options.coordinates, conversion) ||
            !try_inverse(conversion, inverse_conversion))
        {
            return Result(invalid("invalid source coordinate conversion"));
        }
        std::map<std::string, SourceNode> nodes;
        std::vector<const aiNode*> ordered;
        if (!collect_nodes(*scene->mRootNode, Matrix4::identity(), nodes, ordered, 0))
        {
            return Result(invalid("source node hierarchy has duplicate names or exceeds limits"));
        }
        std::set<const aiNode*> required;
        std::size_t deform_count = 0;
        for (unsigned i = 0; i < scene->mNumMeshes; ++i)
        {
            const auto* mesh = scene->mMeshes[i];
            if (!mesh || mesh->mNumAnimMeshes != 0 || mesh->mNumBones > max_skeleton_bones)
            {
                return Result(invalid("morph geometry or oversized source skin is not supported"));
            }
            for (unsigned j = 0; j < mesh->mNumBones; ++j)
            {
                if (!mesh->mBones[j] || !add_ancestors(mesh->mBones[j]->mName.C_Str(), nodes, required))
                {
                    return Result(invalid("deform bone is absent from the source hierarchy"));
                }
                ++deform_count;
            }
        }
        if (deform_count == 0 || required.size() > max_skeleton_bones)
        {
            return Result(invalid("source has no skin or exceeds the skeleton budget"));
        }
        ImportedSkeletalMesh result;
        std::map<std::string, std::uint32_t> bone_indices;
        SkeletalMeshBuildInput input;
        for (const auto* node : ordered)
        {
            if (required.count(node) == 0)
            {
                continue;
            }
            SkeletonBone bone;
            bone.name = node->mName.C_Str();
            bone.parent_index =
                node->mParent ? static_cast<std::int32_t>(bone_indices.at(node->mParent->mName.C_Str())) : -1;
            const auto converted_local =
                conversion * assimp_import::matrix_from_assimp(node->mTransformation) * inverse_conversion;
            if (!try_decompose_transform(converted_local, bone.reference_local_transform))
            {
                return Result(invalid("source bone reference transform is not positive TRS"));
            }
            Matrix4 inverse_bind;
            if (!try_inverse(conversion * nodes.at(bone.name).world * inverse_conversion, inverse_bind))
            {
                return Result(invalid("source bone bind is singular"));
            }
            bone_indices.emplace(bone.name, static_cast<std::uint32_t>(result.skeleton.bones.size()));
            result.skeleton.bones.push_back(bone);
            input.inverse_bind_matrices.push_back(inverse_bind);
        }
        std::vector<bool> bind_seen(result.skeleton.bones.size(), false);
        std::map<unsigned, std::uint32_t> material_slots;
        for (const auto* node : ordered)
        {
            const Matrix4 mesh_world = nodes.at(node->mName.C_Str()).world;
            Matrix4 inverse_mesh_world;
            if (!try_inverse(mesh_world, inverse_mesh_world))
            {
                return Result(invalid("source mesh node transform is singular"));
            }
            const auto vertex_transform = conversion * mesh_world;
            for (unsigned instance = 0; instance < node->mNumMeshes; ++instance)
            {
                if (node->mMeshes[instance] >= scene->mNumMeshes)
                {
                    return Result(invalid("source mesh node contains an invalid mesh index"));
                }
                const auto& mesh = *scene->mMeshes[node->mMeshes[instance]];
                if (!mesh.HasBones() || !mesh.HasPositions() || !mesh.HasNormals() ||
                    mesh.mMaterialIndex >= scene->mNumMaterials ||
                    mesh.mNumVertices > max_import_vertices - input.mesh.vertices.size() ||
                    mesh.mNumFaces > (max_import_vertices - input.mesh.indices.size()) / 3)
                {
                    return Result(invalid("unskinned mesh or invalid/oversized geometry in skeletal source"));
                }
                auto slot = material_slots.find(mesh.mMaterialIndex);
                if (slot == material_slots.end())
                {
                    aiString name;
                    scene->mMaterials[mesh.mMaterialIndex]->Get(AI_MATKEY_NAME, name);
                    slot =
                        material_slots
                            .emplace(mesh.mMaterialIndex, static_cast<std::uint32_t>(input.mesh.material_slots.size()))
                            .first;
                    input.mesh.material_slots.push_back(std::string(name.C_Str()) + "_" +
                                                        std::to_string(mesh.mMaterialIndex));
                }
                const auto first_vertex = static_cast<std::uint32_t>(input.mesh.vertices.size());
                for (unsigned vertex = 0; vertex < mesh.mNumVertices; ++vertex)
                {
                    StaticMeshAssetVertex output;
                    const auto& p = mesh.mVertices[vertex];
                    const auto& n = mesh.mNormals[vertex];
                    output.position = transform_position(vertex_transform, Vector3(p.x, p.y, p.z));
                    if (!try_transform_normal(vertex_transform, Vector3(n.x, n.y, n.z), output.normal))
                    {
                        return Result(invalid("source normal transform failed"));
                    }
                    if (mesh.HasTextureCoords(0))
                    {
                        output.uv0 = Vector2(mesh.mTextureCoords[0][vertex].x, mesh.mTextureCoords[0][vertex].y);
                    }
                    if (mesh.HasVertexColors(0))
                    {
                        const auto& color = mesh.mColors[0][vertex];
                        const float channels[] = {color.r, color.g, color.b, color.a};
                        for (std::size_t channel = 0; channel < output.color.size(); ++channel)
                        {
                            if (!std::isfinite(channels[channel]))
                            {
                                return Result(invalid("nonfinite source vertex color"));
                            }
                            const float bounded = std::min(1.0f, std::max(0.0f, channels[channel]));
                            output.color[channel] = static_cast<std::uint8_t>(std::lround(bounded * 255.0f));
                        }
                    }
                    input.mesh.vertices.push_back(output);
                    input.influences.emplace_back();
                }
                for (unsigned bone = 0; bone < mesh.mNumBones; ++bone)
                {
                    const auto& source_bone = *mesh.mBones[bone];
                    const auto index = bone_indices.at(source_bone.mName.C_Str());
                    const Matrix4 bind = conversion * assimp_import::matrix_from_assimp(source_bone.mOffsetMatrix) *
                                         inverse_mesh_world * inverse_conversion;
                    if (bind_seen[index] &&
                        !is_nearly_equal(input.inverse_bind_matrices[index], bind, skin_bind_tolerance))
                    {
                        return Result(invalid("source mesh instances disagree on bone bind"));
                    }
                    input.inverse_bind_matrices[index] = bind;
                    bind_seen[index] = true;
                    if (source_bone.mNumWeights > mesh.mNumVertices)
                    {
                        return Result(invalid("source bone weight count exceeds vertex count"));
                    }
                    for (unsigned weight = 0; weight < source_bone.mNumWeights; ++weight)
                    {
                        const auto& influence = source_bone.mWeights[weight];
                        if (influence.mVertexId >= mesh.mNumVertices)
                        {
                            return Result(invalid("source bone weight references an invalid vertex"));
                        }
                        input.influences[first_vertex + influence.mVertexId].push_back({index, influence.mWeight});
                    }
                }
                StaticMeshAssetSection section;
                section.first_index = static_cast<std::uint32_t>(input.mesh.indices.size());
                section.material_slot = slot->second;
                const bool mirrored = determinant(vertex_transform) < 0;
                for (unsigned face = 0; face < mesh.mNumFaces; ++face)
                {
                    if (mesh.mFaces[face].mNumIndices != 3)
                    {
                        return Result(invalid("source contains non-triangle primitives"));
                    }
                    for (unsigned corner = 0; corner < 3; ++corner)
                    {
                        const auto index = mesh.mFaces[face].mIndices[mirrored ? 2 - corner : corner];
                        if (index >= mesh.mNumVertices)
                        {
                            return Result(invalid("source face index is outside mesh"));
                        }
                        input.mesh.indices.push_back(first_vertex + index);
                    }
                }
                section.index_count = static_cast<std::uint32_t>(input.mesh.indices.size()) - section.first_index;
                input.mesh.sections.push_back(section);
            }
        }
        const auto built = build_skeletal_mesh(input, skeleton_id, result.skeleton, options.skin);
        if (!built.succeeded())
        {
            return Result(built.status());
        }
        result.mesh = built.value().mesh;
        if (built.value().reduced_vertex_count != 0)
        {
            result.warnings.push_back("Reduced influences on " + std::to_string(built.value().reduced_vertex_count) +
                                      " vertices; maximum discarded normalized weight: " +
                                      std::to_string(built.value().maximum_discarded_weight));
        }
        std::size_t total_samples = 0;
        for (unsigned animation_index = 0; animation_index < scene->mNumAnimations; ++animation_index)
        {
            const auto& animation = *scene->mAnimations[animation_index];
            if (!std::isfinite(animation.mTicksPerSecond) || animation.mTicksPerSecond <= 0 ||
                !std::isfinite(animation.mDuration) || animation.mDuration < 0 ||
                animation.mNumChannels > max_skeleton_bones || animation.mNumMeshChannels != 0 ||
                animation.mNumMorphMeshChannels != 0)
            {
                return Result(invalid("unsupported or invalid source animation"));
            }
            ImportedAnimationSequence imported;
            imported.name = std::string(animation.mName.C_Str()) + "_" + std::to_string(animation_index);
            auto& sequence = imported.sequence;
            sequence.data.skeleton = result.mesh.data.skeleton;
            sequence.data.skeleton_reference_hash = result.mesh.data.skeleton_reference_hash;
            sequence.data.duration = animation.mDuration / animation.mTicksPerSecond;
            sequence.data.sample_rate = options.sample_rate;
            sequence.data.sample_count = animation_sample_count(sequence.data.duration, options.sample_rate);
            if (sequence.data.sample_count == 0 ||
                animation.mNumChannels > (max_animation_samples - total_samples) / sequence.data.sample_count)
            {
                return Result(invalid("source animations exceed sample budget"));
            }
            total_samples += animation.mNumChannels * sequence.data.sample_count;
            std::set<std::uint32_t> tracked;
            for (unsigned channel_index = 0; channel_index < animation.mNumChannels; ++channel_index)
            {
                const auto& channel = *animation.mChannels[channel_index];
                const auto mapped = bone_indices.find(channel.mNodeName.C_Str());
                if (mapped == bone_indices.end() || !tracked.insert(mapped->second).second ||
                    !supported_behaviour(channel.mPreState) || !supported_behaviour(channel.mPostState) ||
                    !vector_keys_valid(channel.mPositionKeys, channel.mNumPositionKeys, animation.mDuration) ||
                    !vector_keys_valid(channel.mScalingKeys, channel.mNumScalingKeys, animation.mDuration) ||
                    !rotation_keys_valid(channel.mRotationKeys, channel.mNumRotationKeys, animation.mDuration))
                {
                    return Result(invalid("unmapped, duplicate or unsupported source animation channel"));
                }
                Transform reference;
                if (!try_decompose_transform(
                        assimp_import::matrix_from_assimp(nodes.at(channel.mNodeName.C_Str()).node->mTransformation),
                        reference))
                {
                    return Result(invalid("source animation reference transform is not TRS"));
                }
                AnimationTrack track;
                track.bone_index = mapped->second;
                for (std::uint32_t sample = 0; sample < sequence.data.sample_count; ++sample)
                {
                    const double time =
                        std::min(sequence.data.duration, static_cast<double>(sample) / options.sample_rate) *
                        animation.mTicksPerSecond;
                    Transform local;
                    local.translation = sample_vector(channel.mPositionKeys, channel.mNumPositionKeys, time,
                                                      reference.translation, channel.mPreState, channel.mPostState);
                    local.scale = sample_vector(channel.mScalingKeys, channel.mNumScalingKeys, time, reference.scale,
                                                channel.mPreState, channel.mPostState);
                    local.rotation = sample_rotation(channel.mRotationKeys, channel.mNumRotationKeys, time,
                                                     reference.rotation, channel.mPreState, channel.mPostState);
                    Transform converted;
                    if (!validate_animation_transform(local).succeeded() ||
                        !try_decompose_transform(conversion * to_matrix(local) * inverse_conversion, converted))
                    {
                        return Result(invalid("source animation sample is not positive TRS"));
                    }
                    track.samples.push_back(converted);
                }
                sequence.tracks.push_back(std::move(track));
            }
            sequence.data.track_count = static_cast<std::uint32_t>(sequence.tracks.size());
            const auto valid = validate_animation_compatibility(sequence, skeleton_id, result.skeleton);
            if (!valid.succeeded())
            {
                return Result(valid);
            }
            result.animations.push_back(std::move(imported));
        }
        result.warnings.push_back("Source materials, textures, cameras and lights are not imported.");
        return Result(std::move(result));
    }
} // namespace toy3d
