#include "gamescene/scene_geometry.h"

#include <utility>

#include "math/length_units.h"
#include "logging/logger.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/material/material_asset_builder.h"

namespace toy3d
{
    namespace
    {
        StaticMeshRef make_builtin_cube(MaterialInstanceRef& owner, const PhysicalPath& shader_entries,
                                        ShaderMapCollectionRef program)
        {
#if !TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING
            (void)owner;
            (void)shader_entries;
            TOY_LOG_ERROR("Builtin scene geometry requires ShaderMapEntry loading.");
            return nullptr;
#else
            ShaderMapProgramKey key;
            key.shader_name = "Toy3d/Surface/Phong";
            key.pass_name = "Forward";
            key.role = shader::ShaderPassRole::Forward;
            key.vertex_factory = shader::VertexFactoryType::Local;
            key.platform = ShaderPlatform::VulkanES31;
            if (!program)
            {
                ShaderMapEntryLoader loader(shader_entries);
                auto loaded = ShaderMapCollection::create_candidate(
                    loader.load_default_collection(key.shader_name, key.platform));
                if (!loaded.succeeded())
                {
                    TOY_LOG_ERROR("Builtin scene shader load failed: {}", loaded.error);
                    return nullptr;
                }
                program = std::move(loaded.collection);
            }

            TextureDesc white_desc;
            white_desc.width = 1u;
            white_desc.height = 1u;
            white_desc.format = PixelFormat::R8G8B8A8UNorm;
            white_desc.row_pitches = {4u};
            white_desc.slice_pitches = {4u};
            white_desc.mip_pixels = {{255u, 255u, 255u, 255u}};
            TextureRef white_texture = Texture::create(std::move(white_desc));
            if (!white_texture)
            {
                return nullptr;
            }

            MaterialAssetData material_data;
            material_data.shader_name = key.shader_name;
            material_data.two_sided = true;
            MaterialTextureValues textures;
            textures.named_defaults.emplace("white", std::move(white_texture));
            auto built = create_material_from_asset(material_data, std::move(program), textures);
            if (!built.succeeded())
            {
                TOY_LOG_ERROR("Builtin scene material build failed: {}", built.status().message);
                return nullptr;
            }
            MaterialInstanceRef material = built.value();
            if (!material)
            {
                return nullptr;
            }

            constexpr float h = meters_to_centimeters(0.75f);
            StaticMeshDesc mesh_desc;
            // Each face has its own normal; sharing corner vertices would smooth
            // the primitive and conceal the direction of editor lights.
            mesh_desc.vertices = {{{-h, -h, -h}, {0, 0, -1}, {0, 0}}, {{h, -h, -h}, {0, 0, -1}, {1, 0}},
                                  {{h, h, -h}, {0, 0, -1}, {1, 1}},   {{-h, h, -h}, {0, 0, -1}, {0, 1}},
                                  {{h, -h, h}, {0, 0, 1}, {0, 0}},    {{-h, -h, h}, {0, 0, 1}, {1, 0}},
                                  {{-h, h, h}, {0, 0, 1}, {1, 1}},    {{h, h, h}, {0, 0, 1}, {0, 1}},
                                  {{-h, -h, h}, {-1, 0, 0}, {0, 0}},  {{-h, -h, -h}, {-1, 0, 0}, {1, 0}},
                                  {{-h, h, -h}, {-1, 0, 0}, {1, 1}},  {{-h, h, h}, {-1, 0, 0}, {0, 1}},
                                  {{h, -h, -h}, {1, 0, 0}, {0, 0}},   {{h, -h, h}, {1, 0, 0}, {1, 0}},
                                  {{h, h, h}, {1, 0, 0}, {1, 1}},     {{h, h, -h}, {1, 0, 0}, {0, 1}},
                                  {{-h, h, -h}, {0, 1, 0}, {0, 0}},   {{h, h, -h}, {0, 1, 0}, {1, 0}},
                                  {{h, h, h}, {0, 1, 0}, {1, 1}},     {{-h, h, h}, {0, 1, 0}, {0, 1}},
                                  {{-h, -h, h}, {0, -1, 0}, {0, 0}},  {{h, -h, h}, {0, -1, 0}, {1, 0}},
                                  {{h, -h, -h}, {0, -1, 0}, {1, 1}},  {{-h, -h, -h}, {0, -1, 0}, {0, 1}}};
            // Analytic face UVs give an exact frame; imported assets use offline MikkTSpace.
            for (std::size_t face = 0u; face < 6u; ++face)
            {
                const auto first = face * 4u;
                const vec3 tangent =
                    glm::normalize(mesh_desc.vertices[first + 1u].position - mesh_desc.vertices[first].position);
                for (std::size_t corner = 0u; corner < 4u; ++corner)
                {
                    mesh_desc.vertices[first + corner].tangent = vec4(tangent, -1.0f);
                }
            }
            mesh_desc.valid_tangent_frame = true;
            // A fixed UInt16 alternative matches the small builtin geometry.
            // Public CCW triangles must face outward, matching the stored normals.
            mesh_desc.indices =
                std::vector<std::uint16_t>{0,  2,  1,  0,  3,  2,  4,  6,  5,  4,  7,  6,  8,  10, 9,  8,  11, 10,
                                           12, 14, 13, 12, 15, 14, 16, 18, 17, 16, 19, 18, 20, 22, 21, 20, 23, 22};
            mesh_desc.sections.push_back({0u, 36u, 0u});
            mesh_desc.material_slots.push_back(material);
            auto mesh = StaticMesh::create(std::move(mesh_desc));
            if (!mesh)
            {
                return nullptr;
            }
            owner = std::move(material);
            return mesh;
#endif
        }
    } // namespace

    StaticMeshRef clone_scene_geometry(const StaticMeshRef& prototype)
    {
        if (!prototype)
        {
            return nullptr;
        }
        StaticMeshDesc desc;
        desc.vertices = prototype->vertices();
        desc.vertex_colors = prototype->vertex_colors();
        desc.indices = prototype->indices();
        desc.sections = prototype->sections();
        desc.material_slots = prototype->material_slots();
        desc.material_slot_names = prototype->material_slot_names();
        desc.default_material_references = prototype->default_material_references();
        desc.valid_tangent_frame = prototype->has_valid_tangent_frame();
        // Each placement has a fresh render-resource lifecycle. Released vertex
        // buffers discard their upload payload and cannot be reused on a redo.
        return StaticMesh::create(std::move(desc));
    }

    // --------------------------------------------------------------------------
    // SceneGeometry: shared builtin geometry and default Material
    // --------------------------------------------------------------------------
    bool SceneGeometry::initialize(const PhysicalPath& shader_entries, ShaderMapCollectionRef program)
    {
        cube_ = make_builtin_cube(material_, shader_entries, std::move(program));
        if (!cube_)
        {
            return false;
        }
        StaticMeshDesc plane;
        constexpr float k_plane_half_extent_cm = meters_to_centimeters(2.5f);
        plane.vertices = {{{-k_plane_half_extent_cm, 0, -k_plane_half_extent_cm}, {0, 1, 0}, {0, 0}},
                          {{k_plane_half_extent_cm, 0, -k_plane_half_extent_cm}, {0, 1, 0}, {1, 0}},
                          {{k_plane_half_extent_cm, 0, k_plane_half_extent_cm}, {0, 1, 0}, {1, 1}},
                          {{-k_plane_half_extent_cm, 0, k_plane_half_extent_cm}, {0, 1, 0}, {0, 1}}};
        // Plane UV axes are +X/+Z with normal +Y, hence handedness -1.
        for (auto& vertex : plane.vertices)
        {
            vertex.tangent = vec4(1, 0, 0, -1);
        }
        plane.valid_tangent_frame = true;
        // A fixed UInt16 alternative matches the small builtin geometry.
        plane.indices = std::vector<std::uint16_t>{0, 2, 1, 0, 3, 2};
        plane.sections.push_back({0, 6, 0});
        plane.material_slots.push_back(material_);
        plane_ = StaticMesh::create(std::move(plane));
        return plane_ != nullptr;
    }

    StaticMeshRef SceneGeometry::instantiate(const std::string& kind) const
    {
        if (kind == "Cube")
        {
            return clone_scene_geometry(cube_);
        }
        if (kind == "Plane")
        {
            return clone_scene_geometry(plane_);
        }
        return {};
    }

    void SceneGeometry::release()
    {
        cube_.reset();
        plane_.reset();
        MaterialInstance::release(material_);
    }
} // namespace toy3d
