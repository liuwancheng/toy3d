#include "asset/asset_catalog.h"
#include "asset/asset_pair_store.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "asset/material/material_asset.h"
#include "asset/texture/texture_asset.h"
#include "asset/texture/builtin_texture_assets.h"
#include "asset_pipeline/environment_import.h"
#include "asset_pipeline/texture_import.h"
#include "asset_pipeline/mesh_tangents.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace toy3d
{
    namespace
    {
        struct TextureSpec
        {
            const char* name;
            const char* source;
            const char* id;
        };

        constexpr std::array<TextureSpec, 3> textures = {
            {{"T_White", "T_White.png", "26e14823067241ee84676de813b2e8c3"},
             {"T_Black", "T_Black.png", "a2d7e64e85c940e583f9ac71f5c44833"},
             {"T_RedBrick", "T_RedBrick.jpg", "a6a53651e980491294a675d094006da0"}}};

        bool publish_texture(NativePlatformFile& platform, AssetPairStore& store, const TypeRegistry& types,
                             const PhysicalPath& source_root, const TextureSpec& spec)
        {
            const auto source = platform.join_relative(source_root, spec.source);
            if (!source.succeeded())
            {
                std::cerr << source.status().message << '\n';
                return false;
            }
            const auto bytes = platform.read_binary(source.value());
            if (!bytes.succeeded())
            {
                std::cerr << bytes.status().message << '\n';
                return false;
            }
            const auto texture = import_texture_image(bytes.value());
            if (!texture.succeeded())
            {
                std::cerr << texture.status().message << '\n';
                return false;
            }
            AssetId id;
            if (!AssetId::parse(spec.id, id))
            {
                std::cerr << "Invalid texture ID\n";
                return false;
            }
            const auto pair = encode_texture_asset_pair(types, id, texture.value());
            if (!pair.succeeded())
            {
                std::cerr << pair.status().message << '\n';
                return false;
            }
            const auto path = VirtualPath::parse(std::string("/Engine/") + spec.name + ".asset");
            if (!path.succeeded())
            {
                std::cerr << path.status().message << '\n';
                return false;
            }
            const AssetStatus published = store.publish(path.value(), pair.value(), FilePublishMode::CreateNew);
            if (!published.succeeded())
            {
                std::cerr << published.message << '\n';
                return false;
            }
            std::cout << spec.name << ": " << texture.value().width << 'x' << texture.value().height << '\n';
            return true;
        }

        bool publish_procedural_assets(AssetPairStore& store, const TypeRegistry& types)
        {
            for (std::size_t index = 3u; index < builtin_texture_assets.size(); ++index)
            {
                const auto& spec = builtin_texture_assets[index];
                Texture2DAsset texture;
                texture.width = texture.height = 1u;
                texture.usage = spec.usage;
                texture.format = PixelFormat::R8G8B8A8UNorm;
                texture.mips.push_back({4u, 4u,
                                        spec.usage == TextureUsage::Normal
                                            ? std::vector<std::uint8_t>{128u, 128u, 255u, 255u}
                                            : std::vector<std::uint8_t>{255u, 255u, 255u, 255u}});
                AssetId id;
                const auto path = VirtualPath::parse(std::string("/Engine/") + spec.asset_name + ".asset");
                if (!AssetId::parse(spec.asset_id, id) || !path.succeeded())
                {
                    return false;
                }
                const auto pair = encode_texture_asset_pair(types, id, texture);
                if (!pair.succeeded() ||
                    !store.publish(path.value(), pair.value(), FilePublishMode::CreateNew).succeeded())
                {
                    std::cerr << "Could not publish procedural texture " << spec.asset_name << '\n';
                    return false;
                }
            }
            // A deterministic studio panorama with two broad softboxes. It is
            // baked offline; rendering adds no hidden diffuse ambient term.
            RgbaFloatImage panorama;
            panorama.width = 128u;
            panorama.height = 64u;
            panorama.pixels.resize(panorama.width * panorama.height * 4u);
            const Vector3 key = normalized_or_zero(Vector3(-0.6f, 0.7f, -0.4f));
            const Vector3 fill = normalized_or_zero(Vector3(0.8f, 0.4f, 0.3f));
            constexpr double pi = 3.14159265358979323846;
            for (std::uint32_t y = 0u; y < panorama.height; ++y)
            {
                for (std::uint32_t x = 0u; x < panorama.width; ++x)
                {
                    const double longitude = ((x + 0.5) / panorama.width - 0.5) * 2.0 * pi;
                    const double latitude = (y + 0.5) / panorama.height * pi;
                    const Vector3 direction(static_cast<float>(std::sin(longitude) * std::sin(latitude)),
                                            static_cast<float>(std::cos(latitude)),
                                            static_cast<float>(std::cos(longitude) * std::sin(latitude)));
                    const float key_value = 8.0f * std::pow(std::max(dot(direction, key), 0.0f), 24.0f);
                    const float fill_value = 3.0f * std::pow(std::max(dot(direction, fill), 0.0f), 12.0f);
                    const std::size_t pixel = (static_cast<std::size_t>(y) * panorama.width + x) * 4u;
                    panorama.pixels[pixel] = 0.12f + key_value + 0.75f * fill_value;
                    panorama.pixels[pixel + 1u] = 0.14f + 0.92f * key_value + 0.85f * fill_value;
                    panorama.pixels[pixel + 2u] = 0.18f + 0.78f * key_value + fill_value;
                    panorama.pixels[pixel + 3u] = 1.0f;
                }
            }
            EnvironmentImportSettings settings;
            settings.face_size = 64u;
            const auto environment = build_environment_asset(panorama, settings);
            AssetId environment_id;
            const auto environment_path = VirtualPath::parse("/Engine/E_Studio.asset");
            if (!environment.succeeded() || !AssetId::parse(builtin_studio_environment_id, environment_id) ||
                !environment_path.succeeded())
            {
                std::cerr << "Could not bake builtin studio Environment\n";
                return false;
            }
            const auto pair = encode_environment_asset_pair(types, environment_id, environment.value());
            if (!pair.succeeded() ||
                !store.publish(environment_path.value(), pair.value(), FilePublishMode::CreateNew).succeeded())
            {
                std::cerr << "Could not publish builtin studio Environment\n";
                return false;
            }
            return true;
        }

        bool publish_courtyard_environment(NativePlatformFile& platform, AssetPairStore& store,
                                           const TypeRegistry& types, const PhysicalPath& source_root)
        {
            const auto source = platform.join_relative(source_root, "EpicQuadPanorama_CC+EV1.hdr");
            if (!source.succeeded())
            {
                std::cerr << source.status().message << '\n';
                return false;
            }
            const auto bytes = platform.read_binary(source.value());
            if (!bytes.succeeded())
            {
                std::cerr << bytes.status().message << '\n';
                return false;
            }
            EnvironmentImportSettings settings;
            settings.face_size = 256u;
            const auto environment = import_environment_hdr(bytes.value(), settings);
            AssetId id;
            if (!environment.succeeded() || !AssetId::parse(builtin_courtyard_environment_id, id))
            {
                std::cerr << (environment.succeeded() ? "Invalid courtyard identity" : environment.status().message)
                          << '\n';
                return false;
            }
            const auto pair = encode_environment_asset_pair(types, id, environment.value());
            const auto path = VirtualPath::parse("/Engine/E_PreviewCourtyard.asset");
            if (!pair.succeeded() || !path.succeeded() ||
                !store.publish(path.value(), pair.value(), FilePublishMode::CreateNew).succeeded())
            {
                std::cerr << "Could not publish preview courtyard Environment\n";
                return false;
            }
            return true;
        }

        bool publish_material_preview_mesh(AssetPairStore& store, const TypeRegistry& types)
        {
            StaticMeshAssetGeometry geometry;
            constexpr std::uint32_t longitude_steps = 32u;
            constexpr std::uint32_t latitude_steps = 16u;
            constexpr float pi = 3.14159265358979323846f;
            for (std::uint32_t latitude = 0u; latitude <= latitude_steps; ++latitude)
            {
                const float v = static_cast<float>(latitude) / latitude_steps;
                for (std::uint32_t longitude = 0u; longitude <= longitude_steps; ++longitude)
                {
                    const float u = static_cast<float>(longitude) / longitude_steps;
                    StaticMeshAssetVertex vertex;
                    vertex.normal = Vector3(std::sin(v * pi) * std::sin(u * 2.0f * pi), std::cos(v * pi),
                                            std::sin(v * pi) * std::cos(u * 2.0f * pi));
                    vertex.position = vertex.normal * 50.0f;
                    vertex.uv0 = Vector2(u, v);
                    geometry.vertices.push_back(vertex);
                }
            }
            for (std::uint32_t latitude = 0u; latitude < latitude_steps; ++latitude)
            {
                for (std::uint32_t longitude = 0u; longitude < longitude_steps; ++longitude)
                {
                    const auto a = latitude * (longitude_steps + 1u) + longitude;
                    const auto b = a + longitude_steps + 1u;
                    if (latitude != 0u)
                    {
                        geometry.indices.insert(geometry.indices.end(), {a, b, a + 1u});
                    }
                    if (latitude + 1u != latitude_steps)
                    {
                        geometry.indices.insert(geometry.indices.end(), {a + 1u, b, b + 1u});
                    }
                }
            }
            geometry.sections.push_back({0u, static_cast<std::uint32_t>(geometry.indices.size()), 0u});
            geometry.material_slots.push_back("Surface");
            const auto built = build_mesh_tangents(geometry);
            AssetId id;
            if (!built.succeeded() || !built.value().geometry.valid_tangent_frame ||
                !AssetId::parse("2b893bd1577f4b9caae5af80fbc6e3d2", id))
            {
                std::cerr << "Could not generate the material preview tangent frame\n";
                return false;
            }
            const auto pair = encode_static_mesh_asset_pair(types, id, built.value().geometry);
            const auto path = VirtualPath::parse("/Engine/S_MaterialPreview.asset");
            if (!pair.succeeded() || !path.succeeded())
            {
                return false;
            }
            const auto published = store.publish(path.value(), pair.value(), FilePublishMode::CreateNew);
            if (!published.succeeded())
            {
                std::cerr << published.message << '\n';
                return false;
            }
            return true;
        }

        AssetRef texture_reference(const char* hex)
        {
            AssetRef reference;
            AssetId::parse(hex, reference.asset_id);
            reference.expected_type = "toy3d.Texture2DAssetData";
            return reference;
        }

        bool publish_material(AssetPairStore& store, const TypeRegistry& types, const char* name, const char* hex,
                              const char* shader_name, const char* texture_parameter, const char* texture_id,
                              const Vector4& color)
        {
            AssetId id;
            if (!AssetId::parse(hex, id))
            {
                std::cerr << "Invalid material ID\n";
                return false;
            }
            MaterialAssetData material;
            material.shader_name = shader_name;
            MaterialParameterOverride color_override;
            color_override.name = "base_color";
            color_override.value = color;
            material.overrides.push_back(color_override);
            MaterialParameterOverride texture_override;
            texture_override.name = texture_parameter;
            texture_override.value = texture_reference(texture_id);
            material.overrides.push_back(texture_override);
            const auto pair = encode_material_asset_pair(types, id, material);
            if (!pair.succeeded())
            {
                std::cerr << pair.status().message << '\n';
                return false;
            }
            const auto path = VirtualPath::parse(std::string("/Engine/") + name + ".asset");
            if (!path.succeeded())
            {
                std::cerr << path.status().message << '\n';
                return false;
            }
            const AssetStatus published = store.publish(path.value(), pair.value(), FilePublishMode::CreateNew);
            if (!published.succeeded())
            {
                std::cerr << published.message << '\n';
                return false;
            }
            std::cout << name << '\n';
            return true;
        }
    } // namespace
} // namespace toy3d

int main(int argc, char** argv)
{
    using namespace toy3d;
    if (argc != 3)
    {
        std::cerr << "Usage: Toy3dDefaultAssets <source-directory> <empty-output-directory>\n";
        return 1;
    }
    NativePlatformFile platform;
    const PhysicalPath source_root(argv[1]);
    const PhysicalPath output_root(argv[2]);
    const FileStatus created = platform.create_directories(output_root);
    if (!created.succeeded())
    {
        std::cerr << created.message << '\n';
        return 1;
    }
    const auto existing = platform.enumerate_directory(output_root);
    if (!existing.succeeded() || !existing.value().empty())
    {
        std::cerr << "Output directory must be empty\n";
        return 1;
    }
    DirectoryFileStoreDesc descriptor;
    descriptor.physical_root = output_root;
    const auto directory = DirectoryFileStore::create(platform, descriptor);
    if (!directory.succeeded())
    {
        std::cerr << directory.status().message << '\n';
        return 1;
    }
    const auto root = VirtualPath::parse("/Engine");
    if (!root.succeeded())
    {
        std::cerr << root.status().message << '\n';
        return 1;
    }
    FileMountDesc mount;
    mount.virtual_root = root.value();
    mount.store = directory.value();
    mount.access = MountAccess::ReadWrite;
    mount.allow_enumeration = true;
    FileSystem files;
    const FileStatus mounted = files.add_mount(mount);
    if (!mounted.succeeded())
    {
        std::cerr << mounted.message << '\n';
        return 1;
    }
    const FileStatus frozen_files = files.freeze();
    if (!frozen_files.succeeded())
    {
        std::cerr << frozen_files.message << '\n';
        return 1;
    }
    TypeRegistry types;
    ReflectionStatus registered = register_texture_asset_types(types);
    if (registered.succeeded())
    {
        registered = register_material_asset_types(types);
    }
    if (registered.succeeded())
    {
        registered = register_static_mesh_asset_types(types);
    }
    if (registered.succeeded())
    {
        registered = types.freeze();
    }
    if (!registered.succeeded())
    {
        std::cerr << registered.message << '\n';
        return 1;
    }
    AssetPairStore store(types, files);
    for (const TextureSpec& texture : textures)
    {
        if (!publish_texture(platform, store, types, source_root, texture))
        {
            return 1;
        }
    }
    if (!publish_procedural_assets(store, types) || !publish_material_preview_mesh(store, types) ||
        !publish_courtyard_environment(platform, store, types, source_root))
    {
        return 1;
    }
    const Vector4 white(1.0f, 1.0f, 1.0f, 1.0f);
    const Vector4 black(0.0f, 0.0f, 0.0f, 1.0f);
    if (!publish_material(store, types, "M_Default", "a82316fbd4a349b8b2dfde23697139fe", "Toy3d/Surface/Phong",
                          "surface_tint_texture", textures[0].id, white) ||
        !publish_material(store, types, "M_White", "7bd54976ce62477a949839d57c39b5a4", "Toy3d/Surface/Unlit",
                          "base_color_texture", textures[0].id, white) ||
        !publish_material(store, types, "M_Black", "d736e0dfa85f468392acc08840331c73", "Toy3d/Surface/Unlit",
                          "base_color_texture", textures[1].id, black) ||
        !publish_material(store, types, "M_Brick", "096dc165e01c4cfdbf5c93e4b831f41f", "Toy3d/Surface/Phong",
                          "surface_tint_texture", textures[2].id, white))
    {
        return 1;
    }
    const auto catalog = scan_asset_catalog(types, files, {root.value()});
    if (!catalog.succeeded() || catalog.value().entries.size() != builtin_texture_assets.size() + 7u)
    {
        std::cerr << (catalog.succeeded() ? "Incomplete default asset catalog" : catalog.status().message) << '\n';
        return 1;
    }
    std::cout << "Validated " << catalog.value().entries.size() << " engine assets\n";
    return 0;
}
