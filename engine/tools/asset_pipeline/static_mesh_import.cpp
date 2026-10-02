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

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }

        // --------------------------------------------------------------------------
        // ImportStream: owns bounded source bytes and Assimp's read cursor
        // --------------------------------------------------------------------------
        class ImportStream final : public Assimp::IOStream
        {
          public:
            explicit ImportStream(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes))
            {
            }
            std::size_t Read(void* buffer, std::size_t size, std::size_t count) override
            {
                if (size == 0 || !buffer)
                {
                    return 0;
                }
                const std::size_t items = std::min(count, (bytes_.size() - cursor_) / size);
                if (items != 0)
                {
                    std::memcpy(buffer, bytes_.data() + cursor_, items * size);
                }
                cursor_ += items * size;
                return items;
            }
            std::size_t Write(const void*, std::size_t, std::size_t) override
            {
                return 0;
            }
            aiReturn Seek(std::size_t offset, aiOrigin origin) override
            {
                std::size_t next = 0;
                if (origin == aiOrigin_SET)
                {
                    next = offset;
                }
                else if (origin == aiOrigin_CUR)
                {
                    if (offset > bytes_.size() - cursor_)
                    {
                        return aiReturn_FAILURE;
                    }
                    next = cursor_ + offset;
                }
                else if (origin == aiOrigin_END)
                {
                    if (offset > bytes_.size())
                    {
                        return aiReturn_FAILURE;
                    }
                    next = bytes_.size() - offset;
                }
                else
                {
                    return aiReturn_FAILURE;
                }
                if (next > bytes_.size())
                {
                    return aiReturn_FAILURE;
                }
                cursor_ = next;
                return aiReturn_SUCCESS;
            }
            std::size_t Tell() const override
            {
                return cursor_;
            }
            std::size_t FileSize() const override
            {
                return bytes_.size();
            }
            void Flush() override
            {
            }

          private:
            std::vector<std::uint8_t> bytes_;
            std::size_t cursor_ = 0;
        };

        // --------------------------------------------------------------------------
        // ImportIO: restricts every Assimp source read to the injected source mount
        // --------------------------------------------------------------------------
        class ImportIO final : public Assimp::IOSystem
        {
          public:
            ImportIO(const FileSystem& files, std::string root) : files_(files), root_(std::move(root))
            {
            }
            bool Exists(const char* file) const override
            {
                const auto path = path_for(file);
                if (!path.succeeded())
                {
                    return false;
                }
                const auto info = files_.stat(path.value());
                return info.succeeded() && info.value().type == FileType::File;
            }
            char getOsSeparator() const override
            {
                return '/';
            }
            Assimp::IOStream* Open(const char* file, const char* mode) override
            {
                if (!mode ||
                    (std::strcmp(mode, "rb") != 0 && std::strcmp(mode, "r") != 0 && std::strcmp(mode, "rt") != 0))
                {
                    return nullptr;
                }
                const auto path = path_for(file);
                if (!path.succeeded())
                {
                    return nullptr;
                }
                constexpr std::size_t max_source_bytes = 64u * 1024u * 1024u;
                const auto bytes = files_.read_binary(path.value(), max_source_bytes);
                if (!bytes.succeeded())
                {
                    failures.push_back(path.value().utf8() + ": " + bytes.status().message);
                    return nullptr;
                }
                // Assimp owns returned streams and closes them through Close().
                return std::make_unique<ImportStream>(bytes.value()).release();
            }
            void Close(Assimp::IOStream* stream) override
            {
                const std::unique_ptr<Assimp::IOStream> owned(stream);
            }
            bool ComparePaths(const char* one, const char* two) const override
            {
                const auto a = path_for(one);
                const auto b = path_for(two);
                return a.succeeded() && b.succeeded() && a.value().utf8() == b.value().utf8();
            }
            std::vector<std::string> failures;

          private:
            FileResult<VirtualPath> path_for(const char* file) const
            {
                std::string text = file ? file : "";
                std::replace(text.begin(), text.end(), '\\', '/');
                if (!text.empty() && text.front() != '/')
                {
                    text = root_ + "/" + text;
                }
                if (text.compare(0, root_.size() + 1, root_ + "/") != 0)
                {
                    text.clear();
                }
                return VirtualPath::parse(text);
            }
            const FileSystem& files_;
            std::string root_;
        };

        Matrix4 matrix_from_assimp(const aiMatrix4x4& source)
        {
            return Matrix4(Vector4(source.a1, source.b1, source.c1, source.d1),
                           Vector4(source.a2, source.b2, source.c2, source.d2),
                           Vector4(source.a3, source.b3, source.c3, source.d3),
                           Vector4(source.a4, source.b4, source.c4, source.d4));
        }

        bool source_conversion(const aiScene& scene, bool fbx, const StaticMeshImportOptions& options, Matrix4& output)
        {
            float unit = options.convert_scene_unit ? options.source_unit_in_centimeters : 1.0f;
            if (fbx && options.convert_scene_unit && options.use_file_unit && scene.mMetaData &&
                scene.mMetaData->HasKey("UnitScaleFactor"))
            {
                // Assimp stores FBX UnitScaleFactor as float, in cm per source unit.
                // Ignore invalid file units when the caller explicitly bypasses them.
                if (!scene.mMetaData->Get("UnitScaleFactor", unit))
                {
                    return false;
                }
            }
            const float scale = unit * options.import_uniform_scale;
            if (!is_finite(scale) || scale <= 0)
            {
                return false;
            }
            output = Matrix4();
            if (!fbx)
            {
                output.at(0, 0) = scale;
                output.at(1, 1) = scale;
                output.at(2, 2) = -scale;
                return true;
            }
            // FBXConverter's root correction is disabled. Metadata drives this one
            // explicit axis/unit conversion; GlobalScale would apply units twice.
            std::int32_t right = 0, up = 1, front = 2;
            std::int32_t right_sign = 1, up_sign = 1, front_sign = -1;
            if (scene.mMetaData)
            {
                if (!scene.mMetaData->Get("CoordAxis", right) || !scene.mMetaData->Get("CoordAxisSign", right_sign) ||
                    !scene.mMetaData->Get("UpAxis", up) || !scene.mMetaData->Get("UpAxisSign", up_sign) ||
                    !scene.mMetaData->Get("FrontAxis", front) || !scene.mMetaData->Get("FrontAxisSign", front_sign))
                {
                    return false;
                }
            }
            if (right < 0 || right > 2 || up < 0 || up > 2 || front < 0 || front > 2 || right == up || right == front ||
                up == front || (right_sign != 1 && right_sign != -1) || (up_sign != 1 && up_sign != -1) ||
                (front_sign != 1 && front_sign != -1))
            {
                return false;
            }
            output = Matrix4(0);
            output.at(right, 0) = right_sign * scale;
            output.at(up, 1) = up_sign * scale;
            output.at(front, 2) = front_sign * scale;
            output.at(3, 3) = 1;
            return true;
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
            const Matrix4 world = parent * matrix_from_assimp(node.mTransformation);
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
        auto io = std::make_unique<ImportIO>(files, root);
        ImportIO* const io_observer = io.get();
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
        if (!source_conversion(*scene, extension == "fbx", options, conversion))
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
