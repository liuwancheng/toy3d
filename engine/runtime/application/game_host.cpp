#include "application/game_host.h"
#include "rendercore/texture/texture_asset_loader.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <iostream>
#include <cctype>

#include "application/application.h"
#include "asset/asset_catalog.h"
#include "asset/game_project.h"
#include "asset/material/material_asset.h"
#include "asset/mesh/static_mesh_asset.h"
#include "asset/animation/animation_asset.h"
#include "asset/texture/texture_asset.h"
#include "config/command_line_parser.h"
#include "config/console_manager.h"
#include "engine.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/component/skeletal_mesh_component.h"
#include "rendercore/geometry/skeletal_mesh_asset_loader.h"
#include "gamescene/scene_assembly.h"
#include "gamescene/scene_geometry.h"
#include "gamescene/scene_view.h"
#include "logging/logger.h"
#include "misc/sha256.h"
#include "shader/shader_map_entry.h"
#include "platform/platform_defines.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/material/material_library.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"

namespace toy3d
{
    namespace
    {
        FileStatus mount_game_directory(FileSystem& files, NativePlatformFile& platform, const PhysicalPath& physical,
                                        const char* root)
        {
            DirectoryFileStoreDesc desc;
            desc.physical_root = physical;
            desc.writable = false;
            desc.debug_name = root;
            const auto store = DirectoryFileStore::create(platform, desc);
            if (!store.succeeded())
            {
                return store.status();
            }
            FileMountDesc mount;
            mount.virtual_root = VirtualPath::parse(root).value();
            mount.store = store.value();
            mount.allow_enumeration = true;
            mount.access = MountAccess::ReadOnly;
            return files.add_mount(mount);
        }
        // --------------------------------------------------------------------------
        // GameApplication: loads a validated scene then leaves gameplay tick to World
        // --------------------------------------------------------------------------
        class GameApplication final : public Application
        {
          public:
            GameApplication(const GameHostPaths& paths, PhysicalPath project_root, PhysicalPath saved,
                            GameModuleRegistration module)
                : paths_(paths), root_(std::move(project_root)), saved_(std::move(saved)), module_(std::move(module))
            {
            }

          protected:
            bool on_initialize() override
            {
                auto registered = register_static_mesh_asset_types(types_);
                if (registered.succeeded())
                {
                    registered = register_animation_asset_types(types_);
                }
                if (registered.succeeded())
                {
                    registered = register_material_asset_types(types_);
                }
                if (registered.succeeded())
                {
                    registered = register_texture_asset_types(types_);
                }
                if (registered.succeeded())
                {
                    registered = register_scene_asset_types(types_);
                }
                if (!registered.succeeded() || !module_.register_types || !module_.register_types(types_, actors_) ||
                    !types_.freeze().succeeded() || !actors_.freeze(types_))
                {
                    TOY_LOG_ERROR("Game type registration failed.");
                    return false;
                }
                auto mounted = mount_game_directory(files_, platform_, paths_.engine_assets, "/Engine");
                if (mounted.succeeded())
                {
                    mounted =
                        mount_game_directory(files_, platform_, PhysicalPath(root_.utf8() + "/asset"), "/Project");
                }
                if (mounted.succeeded())
                {
                    mounted = mount_game_directory(files_, platform_, saved_, "/Saved");
                }
                if (mounted.succeeded())
                {
                    mounted = files_.freeze();
                }
                if (!mounted.succeeded())
                {
                    TOY_LOG_ERROR("Game content mounts: {}", mounted.message);
                    return false;
                }
                const auto scanned = scan_asset_catalog(
                    types_, files_, {VirtualPath::parse("/Engine").value(), VirtualPath::parse("/Project").value()});
                if (!scanned.succeeded())
                {
                    TOY_LOG_ERROR("Game asset catalog: {}", scanned.status().message);
                    return false;
                }
                catalog_ = scanned.value();
                if (!paths_.shader_deployment.empty())
                {
                    ShaderMapEntryLoader loader(paths_.shader_deployment);
                    std::vector<ShaderMapCollectionRef> configurations;
                    std::string error;
                    if (!loader.load_deployment(configurations, error) || configurations.front()->index().policy.editor)
                    {
                        TOY_LOG_ERROR("Game Shader deployment: {}",
                                      error.empty() ? "Player requires Player policy." : error);
                        return false;
                    }
                    for (const auto& configuration : configurations)
                    {
                        shader_maps_.emplace(
                            std::make_pair(configuration->index().shader_name, configuration->index().permutation_key),
                            configuration);
                    }
                }
                const auto phong = load_shader_map("Toy3d/Surface/Phong");
                if (!phong || !geometry_.initialize(PhysicalPath(paths_.deployment.utf8() + "/shader/phong"), phong))
                {
                    return false;
                }
                const auto defaults = geometry_.default_material()->material();
                MaterialTextureValues textures;
                for (const auto& resource : defaults->parameter_schema().resources)
                {
                    const auto found = defaults->desc().texture_defaults.find(resource.parameter_id);
                    if (found != defaults->desc().texture_defaults.end())
                    {
                        textures.named_defaults[resource.default_value] = found->second;
                    }
                }
                materials_ = std::make_unique<MaterialLibrary>(
                    types_, files_,
                    [this]() -> const AssetIndex&
                    {
                        return catalog_.index;
                    },
                    [this](const std::string& name, const std::vector<shader::ShaderPermutationSelection>& selections)
                    {
                        return load_shader_map(name, selections);
                    },
                    std::move(textures));
                materials_->set_default_material(defaults);
                shader_maps_[{defaults->desc().shader_name, defaults->desc().shader_map->index().permutation_key}] =
                    defaults->desc().shader_map;
                auto& arguments = CommandLineParser::get_instance();
                std::string scene =
                    arguments.get_option("PlayScene", ConsoleManager::get_instance().get_string("Game.StartupScene"));
                if (scene.empty())
                {
                    scene = "/Engine/Scenes/Default.scene";
                }
                const auto path = VirtualPath::parse(scene);
                if (!path.succeeded() ||
                    (scene.compare(0, 9, "/Project/") != 0 && scene.compare(0, 8, "/Engine/") != 0 &&
                     scene.compare(0, 12, "/Saved/play/") != 0))
                {
                    TOY_LOG_ERROR("Game startup Scene path is invalid: {}", scene);
                    return false;
                }
                SceneAssetData data;
                const auto read = read_scene_asset(types_, files_, path.value(), data, &catalog_.index);
                if (!read.succeeded())
                {
                    TOY_LOG_ERROR("Game Scene [{}]: {}", scene, read.message);
                    return false;
                }
                SceneAssemblyServices services;
                services.load_environment = [this](const AssetRef& reference, std::string& error) -> TextureRef
                {
                    const auto loaded = load_environment_asset(files_, catalog_.index, reference);
                    if (!loaded.succeeded())
                    {
                        error = loaded.status().message;
                        return {};
                    }
                    return loaded.value();
                };
                services.load_mesh = [this](const SceneMeshData& mesh, std::string& error) -> StaticMeshRef
                {
                    if (!mesh.builtin_mesh.empty())
                    {
                        return geometry_.instantiate(mesh.builtin_mesh);
                    }
                    const auto source = std::find_if(mesh.resources.begin(), mesh.resources.end(),
                                                     [](const SceneResourceBinding& value)
                                                     {
                                                         return value.role == "mesh";
                                                     });
                    const auto* location =
                        source == mesh.resources.end() ? nullptr : catalog_.index.find(source->reference.asset_id);
                    if (!location)
                    {
                        error = "Scene mesh asset is missing.";
                        return {};
                    }
                    const auto loaded = read_static_mesh_asset(files_, location->path);
                    if (!loaded.succeeded())
                    {
                        error = loaded.status().message;
                        return {};
                    }
                    return create_static_mesh_from_asset(loaded.value(), geometry_.default_material());
                };
                services.load_skeletal_mesh = [this](const SceneSkeletalMeshData& mesh)
                {
                    return load_skeletal_mesh_assets(types_, files_, catalog_.index, mesh,
                                                     geometry_.default_material());
                };
                services.assign_material = [this](Actor&, MeshComponent& component, const std::string& slot,
                                                  const AssetRef& reference, std::string& error)
                {
                    const auto loaded = materials_->load(reference);
                    if (!loaded.succeeded())
                    {
                        error = loaded.status().message;
                        return false;
                    }
                    const auto& slots = component.material_slot_names();
                    const auto found = std::find(slots.begin(), slots.end(), slot);
                    if (found == slots.end())
                    {
                        error = "Unknown Material slot: " + slot;
                        return false;
                    }
                    return component.set_material_override(static_cast<std::uint32_t>(found - slots.begin()),
                                                           loaded.value());
                };
                SceneAssemblyResult result;
                std::string error;
                if (!assemble_scene(world(), data, actors_, types_, services, result, error, &catalog_.index))
                {
                    TOY_LOG_ERROR("Game Scene assembly [{}]: {}", scene, error);
                    return false;
                }
                TOY_LOG_INFO("Game loaded Scene [{}] with {} Actors; gameplay starts after renderer binding.", scene,
                             world().actor_count());
                return true;
            }
            void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override
            {
                build_game_scene_views(world(), views, extent);
            }
            void on_shutdown() override
            {
                for (const auto id : world().actor_ids())
                {
                    if (auto* actor = world().find_actor_by_id(id))
                    {
                        if (!world().destroy_actor(*actor))
                        {
                            TOY_LOG_ERROR("Game Actor teardown failed: {}", id);
                        }
                    }
                }
                const auto drained = flush_rendering_commands();
                if (!drained.succeeded())
                {
                    TOY_LOG_ERROR("Game scene teardown could not drain rendering commands.");
                }
                if (materials_)
                {
                    materials_->shutdown();
                    materials_.reset();
                }
                geometry_.release();
            }

          private:
            ShaderMapCollectionRef load_shader_map(
                const std::string& name, const std::vector<shader::ShaderPermutationSelection>& selections = {})
            {
                const auto family = std::find_if(shader_maps_.begin(), shader_maps_.end(),
                                                 [&](const auto& cached)
                                                 {
                                                     return cached.first.first == name;
                                                 });
                if (family != shader_maps_.end())
                {
                    const auto configuration =
                        shader::resolve_shader_permutation(family->second->index().material_domain, selections);
                    if (!configuration.succeeded())
                    {
                        TOY_LOG_ERROR("Game Shader [{}]: {}", name, configuration.errors.front().message);
                        return {};
                    }
                    const auto selected = shader_maps_.find({name, configuration.permutation->key});
                    if (selected == shader_maps_.end())
                    {
                        TOY_LOG_ERROR("Game Shader [{}] configuration {} is not deployed.", name,
                                      sha256_to_hex(configuration.permutation->key));
                        return {};
                    }
                    return selected->second;
                }
                if (!paths_.shader_deployment.empty())
                {
                    TOY_LOG_ERROR("Game Shader [{}] is not present in its verified deployment.", name);
                    return {};
                }
#if TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING
                std::vector<PhysicalPath> entries;
                Sha256Hash publication_hash{};
                bool published = false;
                const auto record_path =
                    VirtualPath::parse("/Saved/shader/" + sha256_to_hex(sha256(name)) + "/current.txt").value();
                const auto record = files_.read_text_utf8(record_path, 4096u);
                if (record.succeeded())
                {
                    std::istringstream lines(record.value());
                    std::string relative, hash, extra;
                    std::getline(lines, relative);
                    std::getline(lines, hash);
                    AssetId request;
                    // C++17 optional distinguishes invalid publication hashes from valid digests.
                    if (relative.size() != 41u || relative.compare(0, 9, "requests/") != 0 ||
                        !AssetId::parse(relative.substr(9), request) || request.hex() != relative.substr(9) ||
                        !sha256_from_hex(hash) || std::getline(lines, extra))
                    {
                        TOY_LOG_ERROR("Game Shader [{}] has an invalid Saved publication record.", name);
                        return {};
                    }
                    const auto shader_root = platform_.canonical(PhysicalPath(saved_.utf8() + "/shader"));
                    const auto target =
                        platform_.canonical(PhysicalPath(saved_.utf8() + "/shader/" + relative + "/entries"));
                    auto comparable = [](std::string value)
                    {
                        std::replace(value.begin(), value.end(), '\\', '/');
                        while (!value.empty() && value.back() == '/')
                        {
                            value.pop_back();
                        }
#if WITH_WIN
                        std::transform(value.begin(), value.end(), value.begin(),
                                       [](unsigned char c)
                                       {
                                           return static_cast<char>(std::tolower(c));
                                       });
#endif
                        return value;
                    };
                    if (!shader_root.succeeded() || !target.succeeded() ||
                        comparable(target.value().utf8())
                                .compare(0, comparable(shader_root.value().utf8()).size() + 1u,
                                         comparable(shader_root.value().utf8()) + "/") != 0)
                    {
                        TOY_LOG_ERROR("Game Shader [{}] artifact directory escapes Saved/shader.", name);
                        return {};
                    }
                    publication_hash = *sha256_from_hex(hash);
                    published = true;
                    entries.push_back(target.value());
                }
                else if (record.status().code != FileErrorCode::NotFound)
                {
                    TOY_LOG_ERROR("Game Shader publication read: {}", record.status().message);
                    return {};
                }
                // Game consumes published artifacts. Source editing/compilation remains an Editor workflow.
                if (entries.empty())
                {
                    const auto deployed =
                        platform_.enumerate_directory(PhysicalPath(paths_.deployment.utf8() + "/shader"));
                    if (deployed.succeeded())
                    {
                        for (const auto& directory : deployed.value())
                        {
                            if (directory.type == FileType::Directory)
                            {
                                entries.push_back(directory.path);
                            }
                        }
                    }
                }
                std::string error;
                for (const auto& entry : entries)
                {
                    ShaderMapEntryLoader loader(entry);
                    std::vector<ShaderMapCollectionRef> configurations;
                    if (!loader.load_family(name, ShaderPlatform::VulkanES31, configurations, error))
                    {
                        if (published)
                        {
                            break;
                        }
                        continue;
                    }
                    if (published && configurations.front()->index().source_hash != publication_hash)
                    {
                        error = "ShaderMap source revision differs from its publication record.";
                        break;
                    }
                    const auto configuration =
                        shader::resolve_shader_permutation(configurations.front()->index().material_domain, selections);
                    if (!configuration.succeeded())
                    {
                        error = configuration.errors.front().message;
                        break;
                    }
                    const auto selected =
                        std::find_if(configurations.begin(), configurations.end(),
                                     [&](const ShaderMapCollectionRef& candidate)
                                     {
                                         return candidate->index().permutation_key == configuration.permutation->key;
                                     });
                    if (selected == configurations.end())
                    {
                        error = "Requested Shader configuration is absent from the verified deployment family.";
                        break;
                    }
                    const auto result = *selected;
                    for (const auto& candidate : configurations)
                    {
                        shader_maps_.emplace(std::make_pair(name, candidate->index().permutation_key), candidate);
                    }
                    return result;
                }
                TOY_LOG_ERROR(
                    "Game Shader [{}] has no validated published ShaderMap: {}. Compile it in the project Editor.",
                    name, error);
#else
                TOY_LOG_ERROR("Game Shader loading is disabled: {}", name);
#endif
                return {};
            }
            std::map<std::pair<std::string, Sha256Hash>, ShaderMapCollectionRef> shader_maps_;
            GameHostPaths paths_;
            PhysicalPath root_;
            PhysicalPath saved_;
            GameModuleRegistration module_;
            NativePlatformFile platform_;
            FileSystem files_;
            TypeRegistry types_;
            ActorTypeRegistry actors_;
            AssetCatalog catalog_;
            SceneGeometry geometry_;
            std::unique_ptr<MaterialLibrary> materials_;
        };
    } // namespace

    int run_game_host(const GameHostPaths& paths, const GameModuleRegistration& module, void* native_instance)
    {
        NativePlatformFile platform;
        const auto descriptor = platform.canonical(paths.descriptor);
        if (!descriptor.succeeded())
        {
            std::cerr << "Game project descriptor: " << descriptor.status().message << '\n';
            return 1;
        }
        const auto parent = platform.parent_path(descriptor.value());
        if (!parent.succeeded())
        {
            std::cerr << parent.status().message << '\n';
            return 1;
        }
        const auto filename = descriptor.value().utf8().substr(descriptor.value().utf8().find_last_of("/\\") + 1u);
        if (filename.size() <= 4u || filename.compare(filename.size() - 4u, 4u, ".toy") != 0)
        {
            std::cerr << "Game needs a .toy descriptor.\n";
            return 1;
        }
        FileSystem bootstrap;
        auto mounted = mount_game_directory(bootstrap, platform, parent.value(), "/Game");
        if (mounted.succeeded())
        {
            mounted = bootstrap.freeze();
        }
        if (!mounted.succeeded())
        {
            std::cerr << mounted.message << '\n';
            return 1;
        }
        const auto project = read_game_project(bootstrap, VirtualPath::parse("/Game/" + filename).value());
        if (!project.succeeded())
        {
            std::cerr << "Game project: " << project.status().message << '\n';
            return 1;
        }
        if (project.value().engine_association != "toy3d_dev" || project.value().modules.size() != 1u ||
            project.value().modules.front().type != GameModuleType::Runtime ||
            project.value().modules.front().name != module.name)
        {
            std::cerr << "Game host requires one matching Runtime module (" << module.name << ").\n";
            return 1;
        }
        const PhysicalPath saved(parent.value().utf8() + "/saved");
        const auto made = platform.create_directories(saved);
        if (!made.succeeded())
        {
            std::cerr << "Game Saved: " << made.message << '\n';
            return 1;
        }
        Engine engine;
        EngineStartupPaths startup;
        startup.engine_assets = paths.engine_assets;
        startup.engine_config = paths.engine_config;
        startup.project_assets = PhysicalPath(parent.value().utf8() + "/asset");
        startup.project_config = PhysicalPath(parent.value().utf8() + "/config");
        startup.saved = saved;
        startup.log_file_name = make_dated_log_file_name("game");
        if (!engine.set_startup_paths(std::move(startup)) || !engine.initialize_logging())
        {
            return 1;
        }
        engine.set_shader_load_config({paths.shader_deployment, false});
        engine.set_application(std::make_unique<GameApplication>(paths, parent.value(), saved, module));
        engine.init(native_instance);
        const bool initialized = engine.initialized();
        if (initialized)
        {
            engine.main_loop();
        }
        engine.exit();
        engine.set_application(nullptr);
        return initialized ? 0 : 1;
    }
} // namespace toy3d
