#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <iostream>

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: Toy3dAssimpProbe <model.fbx|model.obj|model.gltf|model.glb>\n";
        return 2;
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(argv[1], aiProcess_Triangulate);
    if (scene == nullptr || scene->mRootNode == nullptr || scene->mNumMeshes == 0)
    {
        std::cerr << "Assimp import failed: " << importer.GetErrorString() << '\n';
        return 1;
    }

    unsigned int triangle_count = 0;
    for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index)
    {
        const aiMesh* mesh = scene->mMeshes[mesh_index];
        if (mesh == nullptr || mesh->mNumVertices == 0)
        {
            std::cerr << "Assimp returned an empty mesh.\n";
            return 1;
        }
        for (unsigned int face_index = 0; face_index < mesh->mNumFaces; ++face_index)
        {
            if (mesh->mFaces[face_index].mNumIndices != 3)
            {
                std::cerr << "Assimp triangulation left a non-triangle face.\n";
                return 1;
            }
            ++triangle_count;
        }
    }

    if (triangle_count == 0)
    {
        std::cerr << "Assimp returned no triangles.\n";
        return 1;
    }

    std::cout << "meshes=" << scene->mNumMeshes << " triangles=" << triangle_count
              << " material_slots=" << scene->mNumMaterials << '\n';
    return 0;
}
