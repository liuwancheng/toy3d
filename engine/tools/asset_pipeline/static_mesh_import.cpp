#include "static_mesh_import.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <utility>

#include <assimp/Importer.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "math/matrix4.h"
#include "asset_pipeline/static_mesh_builder.h"
#include "asset_pipeline/assimp_import_support.h"

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }

        bool append_mesh(const aiMesh& source, const Matrix4& transform, std::uint32_t slot, MeshDescription& mesh,
                         std::vector<std::string>& warnings)
        {
            constexpr std::size_t limit = 1000000;
            if (source.HasBones() || source.mNumAnimMeshes != 0 || !source.HasPositions() ||
                source.mNumVertices > limit - mesh.positions.size() ||
                source.mNumFaces > (limit - mesh.corners.size()) / 3)
            {
                return false;
            }
            Matrix4 inverse;
            if (!is_finite(transform) || !try_inverse(transform, inverse))
            {
                return false;
            }
            const Matrix4 normal_transform = transpose(inverse);
            const bool mirrored = determinant(transform) < 0;
            const std::uint32_t first_vertex = static_cast<std::uint32_t>(mesh.positions.size());
            for (unsigned i = 0; i < source.mNumVertices; ++i)
            {
                const aiVector3D& p = source.mVertices[i];
                const Vector3 position = transform_position(transform, Vector3(p.x, p.y, p.z));
                if (!is_finite(position))
                {
                    return false;
                }
                mesh.positions.push_back(position);
            }
            if (!source.HasNormals())
            {
                warnings.push_back("Missing normals: generated face normals.");
            }
            if (!source.HasTextureCoords(0))
            {
                warnings.push_back("Missing UV0: filled with zero.");
            }
            if (source.GetNumUVChannels() > 1)
            {
                warnings.push_back("Only UV0 is retained in this version.");
            }
            for (unsigned face_index = 0; face_index < source.mNumFaces; ++face_index)
            {
                const aiFace& face = source.mFaces[face_index];
                if (face.mNumIndices != 3)
                {
                    return false;
                }
                std::array<unsigned, 3> indices{face.mIndices[0], face.mIndices[1], face.mIndices[2]};
                if (mirrored)
                {
                    std::swap(indices[1], indices[2]);
                }
                for (const unsigned index : indices)
                {
                    if (index >= source.mNumVertices)
                    {
                        return false;
                    }
                }
                Vector3 face_normal;
                if (!try_normalize(
                        cross(mesh.positions[first_vertex + indices[1]] - mesh.positions[first_vertex + indices[0]],
                              mesh.positions[first_vertex + indices[2]] - mesh.positions[first_vertex + indices[0]]),
                        face_normal))
                {
                    return false;
                }
                MeshTriangle triangle;
                triangle.material_slot = slot;
                for (std::size_t c = 0; c < indices.size(); ++c)
                {
                    const unsigned index = indices[c];
                    MeshCorner corner;
                    corner.vertex = first_vertex + index;
                    corner.normal = face_normal;
                    if (source.HasNormals())
                    {
                        const aiVector3D& n = source.mNormals[index];
                        if (!try_normalize(transform_vector(normal_transform, Vector3(n.x, n.y, n.z)), corner.normal))
                        {
                            return false;
                        }
                    }
                    if (source.HasTextureCoords(0))
                    {
                        const aiVector3D& uv = source.mTextureCoords[0][index];
                        corner.uv0 = Vector2(uv.x, uv.y);
                        if (!is_finite(corner.uv0))
                        {
                            return false;
                        }
                    }
                    if (source.HasVertexColors(0))
                    {
                        const aiColor4D& color = source.mColors[0][index];
                        const std::array<float, 4> channels{color.r, color.g, color.b, color.a};
                        for (std::size_t channel = 0; channel < channels.size(); ++channel)
                        {
                            if (!is_finite(channels[channel]))
                            {
                                return false;
                            }
                            const float bounded = std::min(1.0f, std::max(0.0f, channels[channel]));
                            corner.color[channel] = static_cast<std::uint8_t>(std::lround(bounded * 255.0f));
                        }
                    }
                    triangle.corners[c] = static_cast<std::uint32_t>(mesh.corners.size());
                    mesh.corners.push_back(corner);
                }
                mesh.triangles.push_back(triangle);
            }
            return true;
        }

        bool append_node(const aiScene& scene, const aiNode& node, const Matrix4& parent, const Matrix4& conversion,
                         std::map<unsigned, std::uint32_t>& slots, ImportedStaticMesh& result, unsigned depth,
                         std::size_t& nodes)
        {
            if (depth > 128 || ++nodes > 100000)
            {
                return false;
            }
            const Matrix4 world = parent * assimp_import::matrix_from_assimp(node.mTransformation);
            for (unsigned i = 0; i < node.mNumMeshes; ++i)
            {
                if (node.mMeshes[i] >= scene.mNumMeshes)
                {
                    return false;
                }
                const aiMesh& source = *scene.mMeshes[node.mMeshes[i]];
                if (source.mMaterialIndex >= scene.mNumMaterials)
                {
                    return false;
                }
                auto slot = slots.find(source.mMaterialIndex);
                if (slot == slots.end())
                {
                    aiString imported_name;
                    const aiReturn named = scene.mMaterials[source.mMaterialIndex]->Get(AI_MATKEY_NAME, imported_name);
                    std::string name = named == aiReturn_SUCCESS ? imported_name.C_Str() : "Material";
                    if (name.empty())
                    {
                        name = "Material";
                    }
                    name += "_" + std::to_string(source.mMaterialIndex);
                    slot = slots
                               .emplace(source.mMaterialIndex,
                                        static_cast<std::uint32_t>(result.mesh.material_slots.size()))
                               .first;
                    result.mesh.material_slots.push_back(std::move(name));
                }
                if (!append_mesh(source, conversion * world, slot->second, result.mesh, result.warnings))
                {
                    return false;
                }
            }
            for (unsigned i = 0; i < node.mNumChildren; ++i)
            {
                if (!node.mChildren[i] ||
                    !append_node(scene, *node.mChildren[i], world, conversion, slots, result, depth + 1, nodes))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    AssetResult<std::vector<ImportedStaticMesh>> import_static_meshes(const FileSystem& files,
                                                                      const VirtualPath& source,
                                                                      const StaticMeshImportOptions& options)
    {
        using Result = AssetResult<std::vector<ImportedStaticMesh>>;
        if (!is_finite(options.import_uniform_scale) || options.import_uniform_scale <= 0)
        {
            return Result(invalid("import scale must be finite and greater than zero"));
        }
        const std::string& path = source.utf8();
        const std::size_t dot = path.find_last_of('.');
        std::string extension = dot == std::string::npos ? "" : path.substr(dot + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        if (extension != "fbx" && extension != "obj" && extension != "gltf" && extension != "glb")
        {
            return Result(invalid("only FBX, OBJ, glTF and GLB are supported"));
        }
        const std::string root = path.substr(0, path.find_last_of('/'));
        auto io = std::make_unique<assimp_import::ImportIO>(files, root);
        assimp_import::ImportIO* const io_observer = io.get();
        Assimp::Importer importer;
        importer.SetIOHandler(io.release());
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION, true);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, false);
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate);
        if (!scene || !scene->mRootNode || scene->mNumMeshes == 0)
        {
            AssetStatus status = invalid("Assimp did not produce static geometry");
            status.virtual_path = path;
            status.message += std::string(": ") + importer.GetErrorString();
            return Result(std::move(status));
        }
        ImportedStaticMesh result;
        Matrix4 conversion;
        if (!assimp_import::source_conversion(*scene, extension == "fbx", options, conversion))
        {
            return Result(invalid("invalid axis metadata or effective unit/scale conversion"));
        }
        std::map<unsigned, std::uint32_t> slots;
        std::size_t nodes = 0;
        if (!append_node(*scene, *scene->mRootNode, Matrix4(), conversion, slots, result, 0, nodes))
        {
            return Result(
                invalid("mesh conversion rejected skin/morph, invalid geometry or transforms, or exceeded limits"));
        }
        if (scene->mNumAnimations)
        {
            result.warnings.push_back("Animations were not imported.");
        }
        if (scene->mNumCameras || scene->mNumLights)
        {
            result.warnings.push_back("Source cameras and lights were not imported.");
        }
        result.warnings.insert(result.warnings.end(), io_observer->failures.begin(), io_observer->failures.end());
        result.warnings.push_back("Material slots retained; source materials and textures were not imported.");
        const AssetStatus valid = validate_mesh_description(result.mesh);
        if (!valid.succeeded())
        {
            return Result(valid);
        }
        std::vector<ImportedStaticMesh> results;
        results.push_back(std::move(result));
        return Result(std::move(results));
    }

#if WITH_EDITORONLY_DATA
    AssetResult<StaticMeshImportAsset> import_static_mesh_asset(const FileSystem& files, const VirtualPath& source,
                                                                const AssetId& id,
                                                                const StaticMeshImportOptions& options)
    {
        const auto imported = import_static_meshes(files, source, options);
        if (!imported.succeeded())
        {
            return AssetResult<StaticMeshImportAsset>(imported.status());
        }
        if (imported.value().size() != 1)
        {
            return AssetResult<StaticMeshImportAsset>(invalid("combined import expected one candidate"));
        }
        const ImportedStaticMesh& candidate = imported.value().front();
        const auto built = build_static_mesh(candidate.mesh);
        if (!built.succeeded())
        {
            return AssetResult<StaticMeshImportAsset>(built.status());
        }
        TypeRegistry types;
        const ReflectionStatus registered = register_static_mesh_asset_types(types);
        if (!registered.succeeded() || !types.freeze().succeeded())
        {
            return AssetResult<StaticMeshImportAsset>(invalid("static mesh schema registration failed"));
        }
        const auto encoded = encode_static_mesh_asset_pair(types, id, built.value());
        if (!encoded.succeeded())
        {
            return AssetResult<StaticMeshImportAsset>(encoded.status());
        }
        return AssetResult<StaticMeshImportAsset>(StaticMeshImportAsset{encoded.value(), candidate.warnings});
    }
#endif
} // namespace toy3d
