#include "asset_pipeline/mesh_tangents.h"

#include <array>
#include <cmath>
#include <map>
#include <utility>

#include <mikktspace.h>

namespace toy3d
{
    namespace
    {
        struct MikkMesh
        {
            const StaticMeshAssetGeometry& source;
            std::vector<Vector4> corner_tangents;
        };

        MikkMesh& mesh(const SMikkTSpaceContext* context)
        {
            return *static_cast<MikkMesh*>(context->m_pUserData);
        }

        const StaticMeshAssetVertex& vertex(const SMikkTSpaceContext* context, int face, int corner)
        {
            const auto& source = mesh(context).source;
            return source
                .vertices[source.indices[static_cast<std::size_t>(face) * 3u + static_cast<std::size_t>(corner)]];
        }

        int face_count(const SMikkTSpaceContext* context)
        {
            return static_cast<int>(mesh(context).source.indices.size() / 3u);
        }

        int corner_count(const SMikkTSpaceContext*, int)
        {
            return 3;
        }

        void get_position(const SMikkTSpaceContext* context, float output[], int face, int corner)
        {
            const auto& value = vertex(context, face, corner).position;
            output[0] = value.x;
            output[1] = value.y;
            output[2] = value.z;
        }

        void get_normal(const SMikkTSpaceContext* context, float output[], int face, int corner)
        {
            Vector3 value;
            try_normalize(vertex(context, face, corner).normal, value);
            output[0] = value.x;
            output[1] = value.y;
            output[2] = value.z;
        }

        void get_uv(const SMikkTSpaceContext* context, float output[], int face, int corner)
        {
            const auto& value = vertex(context, face, corner).uv0;
            output[0] = value.x;
            output[1] = value.y;
        }

        void set_tangent(const SMikkTSpaceContext* context, const float tangent[], float sign, int face, int corner)
        {
            mesh(context).corner_tangents[static_cast<std::size_t>(face) * 3u + static_cast<std::size_t>(corner)] =
                Vector4(tangent[0], tangent[1], tangent[2], sign);
        }

        Vector4 fallback_tangent(const Vector3& normal)
        {
            Vector3 n, tangent;
            try_normalize(normal, n);
            const auto axis = std::abs(n.z) < 0.9f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
            try_normalize(cross(axis, n), tangent);
            return Vector4(tangent.x, tangent.y, tangent.z, 1);
        }
    } // namespace

    AssetResult<MeshTangentBuildResult> build_mesh_tangents(const StaticMeshAssetGeometry& source)
    {
        const auto valid = validate_static_mesh_geometry(source);
        if (!valid.succeeded())
        {
            return AssetResult<MeshTangentBuildResult>(valid);
        }
        MikkMesh input{source, std::vector<Vector4>(source.indices.size())};
        SMikkTSpaceInterface interface{};
        interface.m_getNumFaces = face_count;
        interface.m_getNumVerticesOfFace = corner_count;
        interface.m_getPosition = get_position;
        interface.m_getNormal = get_normal;
        interface.m_getTexCoord = get_uv;
        interface.m_setTSpaceBasic = set_tangent;
        SMikkTSpaceContext context{&interface, &input};
        if (!genTangSpaceDefault(&context))
        {
            return AssetResult<MeshTangentBuildResult>({AssetErrorCode::Value,
                                                        {},
                                                        {},
                                                        "render_geometry",
                                                        {},
                                                        "Pinned MikkTSpace failed to generate corner tangents.",
                                                        {}});
        }
        MeshTangentBuildResult output;
        output.geometry = source;
        output.geometry.valid_tangent_frame = true;
        for (std::size_t triangle = 0u; triangle < source.indices.size(); triangle += 3u)
        {
            const auto a = source.vertices[source.indices[triangle]].uv0;
            const auto b = source.vertices[source.indices[triangle + 1u]].uv0;
            const auto c = source.vertices[source.indices[triangle + 2u]].uv0;
            const float determinant = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            output.geometry.valid_tangent_frame =
                output.geometry.valid_tangent_frame && std::isfinite(determinant) && std::abs(determinant) > 1.0e-10f;
        }
        output.source_vertices.reserve(source.vertices.size());
        for (std::size_t i = 0u; i < source.vertices.size(); ++i)
        {
            output.source_vertices.push_back(static_cast<std::uint32_t>(i));
            output.geometry.vertices[i].tangent = fallback_tangent(source.vertices[i].normal);
        }
        std::vector<bool> assigned(source.vertices.size(), false);
        std::map<std::pair<std::uint32_t, std::array<float, 4>>, std::uint32_t> split_vertices;
        for (std::size_t corner = 0u; corner < source.indices.size(); ++corner)
        {
            const auto original = source.indices[corner];
            auto tangent = input.corner_tangents[corner];
            Vector3 n, t;
            try_normalize(source.vertices[original].normal, n);
            if (!is_finite(tangent) ||
                !try_normalize(
                    Vector3(tangent.x, tangent.y, tangent.z) - n * dot(n, Vector3(tangent.x, tangent.y, tangent.z)), t))
            {
                tangent = fallback_tangent(n);
                output.geometry.valid_tangent_frame = false;
            }
            else
            {
                tangent = Vector4(t.x, t.y, t.z, tangent.w < 0.0f ? -1.0f : 1.0f);
            }
            const auto key = std::make_pair(original, std::array<float, 4>{tangent.x, tangent.y, tangent.z, tangent.w});
            auto found = split_vertices.find(key);
            if (found == split_vertices.end())
            {
                std::uint32_t destination = original;
                if (assigned[original])
                {
                    if (output.geometry.vertices.size() >= 1000000u)
                    {
                        return AssetResult<MeshTangentBuildResult>({AssetErrorCode::Value,
                                                                    {},
                                                                    {},
                                                                    "render_geometry",
                                                                    {},
                                                                    "MikkTSpace splitting exceeds the vertex budget.",
                                                                    {}});
                    }
                    destination = static_cast<std::uint32_t>(output.geometry.vertices.size());
                    output.geometry.vertices.push_back(source.vertices[original]);
                    output.source_vertices.push_back(original);
                }
                assigned[original] = true;
                output.geometry.vertices[destination].tangent = tangent;
                found = split_vertices.emplace(key, destination).first;
            }
            output.geometry.indices[corner] = found->second;
        }
        const auto final_valid = validate_static_mesh_geometry(output.geometry);
        return final_valid.succeeded() ? AssetResult<MeshTangentBuildResult>(std::move(output))
                                       : AssetResult<MeshTangentBuildResult>(final_valid);
    }
} // namespace toy3d
