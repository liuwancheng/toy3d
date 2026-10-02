#include "engine.h"

#include "application/application.h"
#include "input/input_system.h"
#include "imgui.h"
#include "config/command_line_parser.h"
#include "config/console_manager.h"
#include "config/render_backend_shader_platform.h"

#include "platform/platform_defines.h"

#if WITH_WIN
#include "platform/win/win32_platform.h"
#include "platform/win/win32_window.h"
#elif WITH_MAC
#include "platform/mac/mac_platform.h"
#include "platform/mac/mac_window.h"
#elif WITH_ANDROID
#include "platform/android/android_platform.h"
#include "platform/android/android_window.h"
#endif

#include "logging/logger.h"
#include "math/length_units.h"
#include "drivers/rhi/rhi_factory.h"
#include "gamescene/world/world.h"
#include "platform/rhi_surface_factory.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/rendering_thread.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/global_shader_type_registry.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/view/scene_view.h"
#include "renderscene/pass/hit_proxy_pass.h"
#include "renderscene/postprocess/tonemap_pass.h"
#include "renderscene/renderer.h"
#include "renderscene/ui/imgui_renderer.h"
#include "renderscene/view/forward_scene_renderer.h"
#include "threading/task_graph/task_graph.h"
#include "threading/thread_manager.h"
#include "ui/imgui_system.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <utility>

namespace toy3d
{
    namespace
    {
        FileStatus add_directory_mount(FileSystem& file_system, const char* virtual_root,
                                       const std::shared_ptr<DirectoryFileStore>& store, MountAccess access,
                                       bool allow_enumeration, const char* debug_name)
        {
            auto parsed_root = VirtualPath::parse(virtual_root);
            if (!parsed_root.succeeded())
            {
                return parsed_root.status();
            }
            FileMountDesc descriptor;
            descriptor.virtual_root = parsed_root.value();
            descriptor.store = store;
            descriptor.access = access;
            descriptor.allow_enumeration = allow_enumeration;
            descriptor.debug_name = debug_name;
            return file_system.add_mount(descriptor);
        }

    } // namespace

    Engine::Engine() = default;

    Engine::~Engine()
    {
        exit();
    }

    void Engine::set_shader_load_config(ShaderLoadConfig config)
    {
        if (!window)
        {
            shader_load_config = std::move(config);
        }
    }
    void Engine::set_application(std::unique_ptr<Application> value)
    {
        if (!world)
        {
            application = std::move(value);
        }
    }

    bool Engine::initialize_logging(std::shared_ptr<LogBuffer> buffer)
    {
        if (logging_started_)
        {
            return logging_outputs_ready_;
        }
        LogConfig log_config;
        log_config.logger_name = "Toy3dRuntime";
        // filesystem composes the platform-native saved/log path without manual
        // separator handling at the runtime composition root.
        log_config.log_directory =
            std::filesystem::u8path(startup_paths.saved.empty() ? ENGINE_SAVED_ROOT : startup_paths.saved.utf8()) /
            "logs";
        log_config.file_name = startup_paths.log_file_name;
        log_config.memory_output = std::move(buffer);
        std::string log_error;
        logging_outputs_ready_ = Logger::get_instance().init(log_config, &log_error);
        logging_started_ = true;
        return logging_outputs_ready_;
    }

    bool Engine::set_startup_paths(EngineStartupPaths paths)
    {
        if (logging_started_ || file_system.frozen())
        {
            return false;
        }
        startup_paths = std::move(paths);
        return true;
    }

    bool Engine::pre_init()
    {
        initialize_logging();
        const auto status = initialize_file_system();
        if (!status.succeeded())
        {
            TOY_LOG_ERROR("Runtime file system initialization: {}", status.message);
            return false;
        }
        auto& console = ConsoleManager::get_instance();
        const auto base = VirtualPath::parse("/Engine/Config/base_engine.ini");
        const auto loaded = console.load_config(file_system, base.value());
        if (!loaded.succeeded())
        {
            TOY_LOG_ERROR("Load base_engine.ini: {}", loaded.message);
            return false;
        }
        if (!startup_paths.project_config.empty())
        {
            const auto project = VirtualPath::parse("/Project/Config/game_engine.ini");
            const auto overlay = console.load_config(file_system, project.value(), ConfigLoadMode::Overlay);
            if (!overlay.succeeded() && overlay.code != FileErrorCode::NotFound)
            {
                TOY_LOG_ERROR("Load game_engine.ini: {}", overlay.message);
                return false;
            }
        }
        CommandLineParser::get_instance().apply_config();
        // The same parser validates typed command-line overrides without silently
        // turning malformed input into an unrelated getter default.
        const auto effective = ConsoleManager::encode_config(console.snapshot());
        if (!effective.succeeded())
        {
            TOY_LOG_ERROR("Effective configuration: {}", effective.status().message);
            return false;
        }
        return true;
    }

    void Engine::init(void* hInstance)
    {
        if (!pre_init())
        {
            exit();
            return;
        }
        // 1.创建平台
#if WITH_WIN
        platform = std::make_unique<Win32Platform>();
#elif WITH_MAC
        platform = std::make_unique<MacPlatform>();
#elif WITH_ANDROID
        platform = std::make_unique<AndroidPlatform>();
#endif

        if (!platform || !platform->init())
        {
            TOY_LOG_ERROR("Runtime platform initialization failed.");
            exit();
            return;
        }
        platform_initialized = true;

        // 2.创建窗口
#if WITH_WIN
        window = std::make_unique<Win32Window>(static_cast<HINSTANCE>(hInstance));
#elif WITH_MAC
        window = std::make_unique<MacWindow>();
#elif WITH_ANDROID
        window = std::make_unique<AndroidWindow>();
#endif

        if (!window)
        {
            TOY_LOG_ERROR("Runtime window creation failed.");
            exit();
            return;
        }

        const bool enable_imgui = ConsoleManager::get_instance().get_bool("Renderer.EnableImGui", true);
        if (enable_imgui)
        {
            imgui_system = std::make_unique<ImGuiSystem>();
            const ImGuiSystemStatus imgui_status = imgui_system->initialize(*window);
            if (!imgui_status.succeeded())
            {
                TOY_LOG_ERROR("Runtime ImGui initialization failed: {}", imgui_status.message);
                exit();
                return;
            }
        }

        RHIResult<RHISurfaceRef> created_surface = create_rhi_surface(*window);
        if (!created_surface.succeeded())
        {
            TOY_LOG_ERROR("Runtime RHI surface creation failed: {}", created_surface.status().message());
            exit();
            return;
        }
        rhi_surface = std::move(created_surface).value();

        BuiltinMeshPassPrograms mesh_pass_programs;
        if (!initialize_builtin_shader_programs(mesh_pass_programs))
        {
            exit();
            return;
        }

        if (!initialize_render_framework(std::move(mesh_pass_programs)))
        {
            exit();
        }
    }

    bool Engine::initialize_render_framework(BuiltinMeshPassPrograms mesh_pass_programs)
    {
        thread_manager = std::make_unique<ThreadManager>();

        const bool use_rendering_thread = ConsoleManager::get_instance().get_bool("Renderer.MultiThreaded", true);
        TaskGraphConfig task_graph_config;
        task_graph_config.multithreaded = use_rendering_thread;
        TaskGraphCreateResult created_task_graph = create_task_graph(task_graph_config, *thread_manager);
        if (!created_task_graph.succeeded())
        {
            TOY_LOG_ERROR("Runtime Task Graph creation failed: {}", created_task_graph.status().message);
            shutdown_render_framework();
            return false;
        }
        task_graph = created_task_graph.take_task_graph();

        const TaskGraphStatus attached = task_graph->attach_to_thread(NamedThread::GameThread);
        if (!attached.succeeded())
        {
            TOY_LOG_ERROR("GameThread attach failed: {}", attached.message);
            shutdown_render_framework();
            return false;
        }

        const Extent window_extent = window->get_win_size();
        RHIViewportContextDesc viewport_desc;
        viewport_desc.extent = window_extent;
        viewport_desc.debug_name = "PrimaryViewport";
        renderer = std::make_unique<Renderer>(
            *task_graph, rhi_surface, std::move(viewport_desc),
            []()
            {
                return create_default_rhi_device();
            },
            global_shader_map,
            imgui_system ? std::make_unique<ImGuiFontAtlasData>(imgui_system->font_atlas()) : nullptr,
            application && application->uses_preview_scene(), std::move(mesh_pass_programs),
            application && application->uses_play_scene());
        rendering_thread = std::make_unique<RenderingThread>(*thread_manager, *task_graph,
                                                             use_rendering_thread ? RenderingThreadMode::MultiThread
                                                                                  : RenderingThreadMode::SingleThread);
        const ThreadStatus started = rendering_thread->start(
            [this]()
            {
                return renderer->initialize();
            });
        if (!started.succeeded())
        {
            TOY_LOG_ERROR("RenderingThread startup failed: {}", started.message);
            shutdown_render_framework();
            return false;
        }

        frame_end_sync = std::make_unique<FrameEndSync>(
            ConsoleManager::get_instance().get_bool("Renderer.AllowOneFrameThreadLag", true),
            [this]()
            {
                if (!renderer)
                {
                    return RenderFenceWaitResult::reached();
                }
                const RendererStatus renderer_status = renderer->status();
                if (renderer_status.lifecycle_state() == RendererLifecycleState::Terminal)
                {
                    return RenderFenceWaitResult::renderer_terminal(renderer_status.error_message());
                }
                return RenderFenceWaitResult::reached();
            });
        world = std::make_unique<World>();
        if (application)
        {
            // Binding starts before the project hook so a partially initialized
            // Application still receives one shutdown callback for rollback.
            application_bound = true;
            if (!application->initialize(*world, *window))
            {
                TOY_LOG_ERROR("Application initialization failed.");
                shutdown_render_framework();
                return false;
            }
        }
        if (application && application->uses_preview_scene() &&
            (!renderer->preview_scene_interface() ||
             !application->on_initialize_preview_scene(*renderer->preview_scene_interface(), *task_graph)))
        {
            TOY_LOG_ERROR("Application could not initialize its preview scene.");
            shutdown_render_framework();
            return false;
        }
        world->initialize();
        if (application && application->uses_play_scene())
        {
            if (!renderer->play_scene_interface())
            {
                TOY_LOG_ERROR("Application Play Scene is unavailable.");
                shutdown_render_framework();
                return false;
            }
            application->on_initialize_play_scene(*renderer->play_scene_interface());
        }
        if (!renderer->scene_interface() || !world->bind_scene(*renderer->scene_interface()))
        {
            TOY_LOG_ERROR("Runtime World could not bind the Renderer scene.");
            shutdown_render_framework();
            return false;
        }
        if (!application || application->starts_world_play())
        {
            world->begin_play();
        }
        return true;
    }

    void Engine::submit_frame_draw(std::unique_ptr<ImGuiDrawData> ui_draw_data, ViewportFrameOutput output)
    {
        if (!window || !renderer || !renderer->scene_interface())
        {
            return;
        }

        const Extent extent = window->get_win_size();
        if (extent.width == 0 || extent.height == 0)
        {
            return;
        }
        output.window_extent = extent;
        output.play_scene = application && application->renders_play_scene();
        output.scene_feedback = application ? application->scene_render_feedback() : nullptr;
        SceneInterface* active_scene =
            output.play_scene ? renderer->play_scene_interface() : renderer->scene_interface();
        if (!active_scene)
        {
            return;
        }
        const Extent scene_extent = output.sample_in_ui ? output.scene_extent : extent;

        std::vector<SceneView> views;
        if (scene_extent.width != 0u && scene_extent.height != 0u && application)
        {
            application->build_scene_views(views, scene_extent);
        }
        else if (scene_extent.width != 0u && scene_extent.height != 0u)
        {
            views.emplace_back(Vector3(0.0f, meters_to_centimeters(1.5f), meters_to_centimeters(-6.0f)),
                               Quaternion::identity(), Vector3(0.0f, 0.0f, 1.0f),
                               IntRect{0, 0, scene_extent.width, scene_extent.height}, scene_extent,
                               CameraProjectionMode::Perspective, to_radians(Degrees(60.0f)),
                               meters_to_centimeters(0.1f), meters_to_centimeters(1000.0f));
        }
        if (views.empty() && scene_extent.width != 0u && scene_extent.height != 0u)
        {
            TOY_LOG_ERROR("Runtime frame draw requires at least one SceneView.");
            return;
        }

        std::unique_ptr<SceneRenderer> scene_renderer;
        if (!views.empty())
        {
            scene_renderer =
                std::make_unique<ForwardSceneRenderer>(SceneViewFamily(*active_scene, scene_extent, std::move(views)));
        }
        UiRenderWork work;
        std::unique_ptr<SceneRenderer> preview_renderer;
        if (application)
        {
            application->on_collect_ui_render_work(work);
        }
        if (application)
        {
            std::vector<MaterialProgramValidationRef> validations;
            application->on_collect_material_validation(validations);
            for (auto& validation : validations)
            {
                renderer->validate_material_program(std::move(validation));
            }
            std::vector<BuiltinShaderUpdateRef> builtin_updates;
            application->on_collect_builtin_shader_updates(builtin_updates);
            for (auto& update : builtin_updates)
            {
                renderer->prepare_builtin_shaders(std::move(update));
            }
        }
        if (work.preview.request_id && renderer->preview_scene_interface())
        {
            preview_renderer = std::make_unique<ForwardSceneRenderer>(
                SceneViewFamily(*renderer->preview_scene_interface(), work.preview.extent,
                                std::move(work.preview.views)),
                true);
        }
        renderer->draw_frame(std::move(scene_renderer), std::move(ui_draw_data), output, std::move(work),
                             std::move(preview_renderer));
    }

    void Engine::shutdown_render_framework()
    {
        frame_end_sync.reset();

        if (world)
        {
            if (application_bound && application)
            {
                application->shutdown();
                application_bound = false;
            }
            world->end_play();
            if (world->scene_interface() != nullptr)
            {
                static_cast<void>(world->unbind_scene());
            }
            world.reset();
        }

        if (rendering_thread)
        {
            if (rendering_thread->is_ready())
            {
                const RenderFenceWaitResult drained = flush_rendering_commands(
                    [this]()
                    {
                        if (!renderer)
                        {
                            return RenderFenceWaitResult::reached();
                        }
                        const RendererStatus renderer_status = renderer->status();
                        return renderer_status.lifecycle_state() == RendererLifecycleState::Terminal
                                   ? RenderFenceWaitResult::renderer_terminal(renderer_status.error_message())
                                   : RenderFenceWaitResult::reached();
                    });
                if (!drained.rendering_thread_reached())
                {
                    TOY_LOG_ERROR("Rendering command drain failed during shutdown: {}",
                                  drained.framework_status().message);
                }
            }

            const ThreadStatus stopped = rendering_thread->stop(
                [this]()
                {
                    return renderer != nullptr ? renderer->teardown() : ThreadStatus::success();
                });
            if (!stopped.succeeded())
            {
                TOY_LOG_ERROR("RenderingThread shutdown failed: {}", stopped.message);
            }
            rendering_thread.reset();
        }

        renderer.reset();
        global_shader_map.reset();
        builtin_shader_map.reset();
        builtin_shader_loader.reset();

        if (task_graph)
        {
            const TaskGraphShutdownResult stopped = task_graph->shutdown(TaskGraphShutdownMode::Drain);
            if (!stopped.succeeded())
            {
                TOY_LOG_ERROR("Task Graph shutdown failed: {}", stopped.status.message);
            }
            task_graph.reset();
        }
        thread_manager.reset();
        imgui_system.reset();
    }

    bool Engine::initialize_builtin_shader_programs(BuiltinMeshPassPrograms& mesh_pass_programs)
    {
#if TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING
        const PhysicalPath deployment_root(ENGINE_ASSET_ROOT);
        auto shader_root = native_platform_file.join_relative(deployment_root, "shader");
        if (!shader_root.succeeded())
        {
            TOY_LOG_ERROR("Built-in ShaderMap root could not be resolved: {}", shader_root.status().message);
            return false;
        }
        auto output_root = native_platform_file.join_relative(shader_root.value(), "output");
        if (!output_root.succeeded())
        {
            TOY_LOG_ERROR("Built-in output ShaderMap root could not be resolved: {}", output_root.status().message);
            return false;
        }

        builtin_shader_loader = std::make_unique<ShaderMapEntryLoader>(output_root.value());
        builtin_shader_map = std::make_unique<ShaderMap>(*builtin_shader_loader);

        ShaderPlatform shader_platform = ShaderPlatform::D3D11SM5;
        std::string platform_error;
        if (!try_get_shader_platform_for_backend(configured_rhi_backend_name(), shader_platform, platform_error))
        {
            TOY_LOG_ERROR("Built-in Shader platform selection failed: {}", platform_error);
            return false;
        }

        GlobalShaderTypeRegistryResult registered_types = GlobalShaderTypeRegistry::get().freeze();
        if (!registered_types.succeeded())
        {
            TOY_LOG_ERROR("Global Shader type registration failed: {}", registered_types.error);
            return false;
        }

        GlobalShaderRequirements requirements(registered_types.types);
        std::string requirement_error;
        if (!requirements.add(tonemap_global_shader_type(), requirement_error))
        {
            TOY_LOG_ERROR("Tonemap Global Shader requirement failed: {}", requirement_error);
            return false;
        }
        if (!requirements.add(hit_proxy_global_shader_type(), requirement_error))
        {
            TOY_LOG_ERROR("HitProxy Global Shader requirement failed: {}", requirement_error);
            return false;
        }
        if (imgui_system != nullptr)
        {
            if (!requirements.add(imgui_global_shader_type(), requirement_error))
            {
                TOY_LOG_ERROR("ImGui Global Shader requirement failed: {}", requirement_error);
                return false;
            }
        }

        GlobalShaderMapResult loaded =
            GlobalShaderMap::load(*builtin_shader_map, shader_platform, requirements.types());
        if (!loaded.succeeded())
        {
            TOY_LOG_ERROR("Built-in GlobalShaderMap failed to load: {}", loaded.error);
            return false;
        }
        global_shader_map = std::move(loaded.shader_map);
        ShaderMapProgramKey shadow_key;
        shadow_key.shader_name = "Toy3d/ShadowDepth/Default";
        shadow_key.pass_name = "ShadowDepth";
        shadow_key.platform = shader_platform;
        ShaderMapProgramResult shadow_loaded = builtin_shader_map->find_or_load(shadow_key);
        if (!shadow_loaded.succeeded())
        {
            TOY_LOG_ERROR("Built-in ShadowDepth ShaderMap failed to load: {}", shadow_loaded.error);
            return false;
        }
        mesh_pass_programs.shadow_depth_default = std::move(shadow_loaded.program);
        return true;
#else
        TOY_LOG_ERROR("Built-in output ShaderMap loading requires a supported runtime loader.");
        return false;
#endif
    }

    FileStatus Engine::initialize_file_system()
    {
        if (file_system.frozen())
        {
            return FileStatus::success();
        }
        const PhysicalPath deployment_root(ENGINE_ASSET_ROOT);
        const PhysicalPath saved_root =
            startup_paths.saved.empty() ? PhysicalPath(ENGINE_SAVED_ROOT) : startup_paths.saved;
        auto shader_root = native_platform_file.join_relative(deployment_root, "shader");
        if (!shader_root.succeeded())
        {
            return shader_root.status();
        }
        auto asset_root = startup_paths.engine_assets.empty()
                              ? native_platform_file.join_relative(deployment_root, "engine/asset")
                              : FileResult<PhysicalPath>(startup_paths.engine_assets);
        if (!asset_root.succeeded())
        {
            return asset_root.status();
        }
        auto temp_root = native_platform_file.join_relative(saved_root, "temp");
        if (!temp_root.succeeded())
        {
            return temp_root.status();
        }

        FileStatus status = native_platform_file.create_directories(saved_root);
        if (!status.succeeded())
        {
            return status;
        }
        status = native_platform_file.create_directories(temp_root.value());
        if (!status.succeeded())
        {
            return status;
        }

        auto create_store = [this](const PhysicalPath& root, bool writable, const char* debug_name)
        {
            DirectoryFileStoreDesc descriptor;
            descriptor.physical_root = root;
            descriptor.writable = writable;
            descriptor.symlink_policy = DirectorySymlinkPolicy::Deny;
            descriptor.debug_name = debug_name;
            return DirectoryFileStore::create(native_platform_file, descriptor);
        };

        auto engine_assets = create_store(asset_root.value(), false, "RuntimeEngineAssets");
        if (!engine_assets.succeeded())
        {
            return engine_assets.status();
        }
        engine_asset_store = engine_assets.value();
        auto engine_shaders = create_store(shader_root.value(), false, "RuntimeEngineShaders");
        if (!engine_shaders.succeeded())
        {
            return engine_shaders.status();
        }
        engine_shader_store = engine_shaders.value();
        auto saved = create_store(saved_root, true, "RuntimeSaved");
        if (!saved.succeeded())
        {
            return saved.status();
        }
        saved_store = saved.value();
        auto temp = create_store(temp_root.value(), true, "RuntimeTemp");
        if (!temp.succeeded())
        {
            return temp.status();
        }
        temp_store = temp.value();

        status = add_directory_mount(file_system, "/Engine", engine_asset_store, MountAccess::ReadOnly, true, "Engine");
        if (!status.succeeded())
        {
            return status;
        }
        status = add_directory_mount(file_system, "/Engine/Shader", engine_shader_store, MountAccess::ReadOnly, true,
                                     "EngineShader");
        if (!status.succeeded())
        {
            return status;
        }
        // Deployment directories have separate read-only stores; /Project must
        // never alias the engine assets. FileSystem owns these store lifetimes.
        const auto config_root = startup_paths.engine_config.empty()
                                     ? native_platform_file.join_relative(deployment_root, "engine/config")
                                     : FileResult<PhysicalPath>(startup_paths.engine_config);
        if (!config_root.succeeded())
        {
            return config_root.status();
        }
        const auto base_config = create_store(config_root.value(), false, "EngineConfig");
        if (!base_config.succeeded())
        {
            return base_config.status();
        }
        status = add_directory_mount(file_system, "/Engine/Config", base_config.value(), MountAccess::ReadOnly, true,
                                     "EngineConfig");
        if (!status.succeeded())
        {
            return status;
        }
        if (!startup_paths.project_assets.empty())
        {
            const auto assets = create_store(startup_paths.project_assets, false, "ProjectAssets");
            if (!assets.succeeded())
            {
                return assets.status();
            }
            status = add_directory_mount(file_system, "/Project", assets.value(), MountAccess::ReadOnly, true,
                                         "ProjectAssets");
            if (!status.succeeded())
            {
                return status;
            }
        }
        if (!startup_paths.project_config.empty())
        {
            const auto config = create_store(startup_paths.project_config, false, "ProjectConfig");
            if (!config.succeeded())
            {
                return config.status();
            }
            status = add_directory_mount(file_system, "/Project/Config", config.value(), MountAccess::ReadOnly, true,
                                         "ProjectConfig");
            if (!status.succeeded())
            {
                return status;
            }
        }
        status = add_directory_mount(file_system, "/Saved", saved_store, MountAccess::ReadWrite, true, "Saved");
        if (!status.succeeded())
        {
            return status;
        }
        status = add_directory_mount(file_system, "/Temp", temp_store, MountAccess::ReadWrite, true, "Temp");
        if (!status.succeeded())
        {
            return status;
        }
        status = file_system.freeze();
        if (!status.succeeded())
        {
            return status;
        }

        return FileStatus::success();
    }

    void Engine::main_loop()
    {
        if (!window || !frame_end_sync)
        {
            return;
        }
        auto previous_tick = std::chrono::steady_clock::now();
        while (true)
        {
            const auto current_tick = std::chrono::steady_clock::now();
            delta_time = std::chrono::duration<double>(current_tick - previous_tick).count();
            previous_tick = current_tick;
            game_time += delta_time;
            ++frame_count;

            window->process_events();
            if (application_bound && application && renderer)
            {
                HitProxyResult hit;
                while (renderer->poll_hit_proxy(hit))
                {
                    application->hit_proxy_result(hit);
                }
                UiTextureResult ui_result;
                while (renderer->poll_ui_texture(ui_result))
                {
                    application->on_ui_texture_result(std::move(ui_result));
                }
            }
            if (world)
            {
                if (world->lifecycle_state() == WorldLifecycleState::Playing)
                {
                    static_cast<void>(world->tick(delta_time));
                }
                if (application_bound && application)
                {
                    application->tick(delta_time);
                }
            }
            if (window->should_close())
            {
                if (!application_bound || !application || application->on_close_requested())
                {
                    break;
                }
                if (!window->cancel_close())
                {
                    TOY_LOG_ERROR("Platform window cannot defer the requested close.");
                    break;
                }
            }
            std::unique_ptr<ImGuiDrawData> ui_draw_data;
            ViewportFrameOutput viewport_output;
            if (imgui_system && imgui_system->begin_frame(*window, delta_time))
            {
                if (application_bound && application)
                {
                    application->build_ui();
                    viewport_output.sample_in_ui = application->scene_viewport_extent(viewport_output.scene_extent);
                    if (viewport_output.sample_in_ui)
                    {
                        viewport_output.texture_id = IMGUI_SCENE_VIEWPORT_TEXTURE_ID;
                    }
                }
                const ImGuiTextureId allowed_texture = viewport_output.sample_in_ui &&
                                                               viewport_output.scene_extent.width != 0u &&
                                                               viewport_output.scene_extent.height != 0u
                                                           ? viewport_output.texture_id
                                                           : ImGuiTextureId{};
                ImGuiSnapshotResult ui_result = imgui_system->end_frame(
                    allowed_texture, application ? application->ui_texture_ids() : std::vector<ImGuiTextureId>{});
                bool game_mouse = false;
                bool game_keyboard = false;
                if (application && application->game_viewport_input(game_mouse, game_keyboard))
                {
                    const bool text = ImGui::GetIO().WantTextInput;
                    InputSystem::get_instance().set_capture_policy({!game_mouse || text, !game_keyboard || text, true});
                }
                if (!ui_result.succeeded())
                {
                    TOY_LOG_ERROR("Runtime UI frame was rejected: {}", ui_result.diagnostic);
                }
                else
                {
                    ui_draw_data = std::move(ui_result.draw_data);
                    if (viewport_output.sample_in_ui && application_bound && application)
                    {
                        static_cast<void>(application->hit_proxy_request(viewport_output.hit_proxy_request));
                    }
                }
            }
            submit_frame_draw(std::move(ui_draw_data), viewport_output);
            const RenderFenceWaitResult synchronized = frame_end_sync->sync_frame();
            if (!synchronized.succeeded())
            {
                if (synchronized.has_renderer_terminal())
                {
                    TOY_LOG_ERROR("Renderer entered terminal state: {}", synchronized.renderer_error());
                }
                else
                {
                    TOY_LOG_ERROR("Frame synchronization failed: {}", synchronized.framework_status().message);
                }
                break;
            }
        }
    }

    void Engine::exit()
    {
        if (engine_exited)
        {
            return;
        }
        engine_exited = true;
        shutdown_render_framework();
        rhi_surface.reset();
        window.reset();
        if (platform_initialized && platform)
        {
            platform->exit();
            platform_initialized = false;
        }
        platform.reset();
        // todo: resource的释放、文件系统的关闭、游戏模块的关闭等
        Logger::get_instance().exit();
    }

} // namespace toy3d
