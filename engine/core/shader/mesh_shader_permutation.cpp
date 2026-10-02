#include "shader/mesh_shader_permutation.h"

#include <vector>

namespace toy3d::shader
{
    Sha256Hash mesh_shader_permutation_key(const Sha256Hash& material_key, MeshVertexFactoryType factory)
    {
        if (factory == MeshVertexFactoryType::Local)
        {
            return material_key;
        }
        // Versioned, domain-separated bytes. Local preserves existing material identities.
        const char domain[] = "Toy3dMeshVertexFactory";
        std::vector<std::uint8_t> bytes(domain, domain + sizeof(domain) - 1);
        bytes.insert(bytes.end(), {1, 0, 0, 0, 1, 0, 0, 0});
        bytes.insert(bytes.end(), material_key.begin(), material_key.end());
        return sha256(bytes);
    }
} // namespace toy3d::shader
