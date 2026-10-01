#include "scene/editor_scene_session.h"

#include <filesystem>
#include <algorithm>
#include <iostream>
#include "assets/asset_editor_registry.h"
#include "file_system/directory_file_store.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/world/world.h"
#include "panels/editor_panel_registry.h"
#include "panels/scene_panels.h"
#include "assets/texture/texture_preview_panel.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "threading/task_graph/task_graph.h"
#include "threading/thread_manager.h"
#include "scene/editor_selection.h"
#include "viewport/scene_viewport.h"
#include "workspace/editor_workspace.h"

namespace
{
    int failures = 0;
    void check(bool result, const char* message)
    {
        if (!result) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
    }
    toy3d::VirtualPath virtual_path(const char* text)
    {
        const auto parsed = toy3d::VirtualPath::parse(text);
        check(parsed.succeeded(), "Virtual path parses");
        return parsed.succeeded() ? parsed.value() : toy3d::VirtualPath{};
    }

    void check_details_drag(toy3d::EditorWorkspace& workspace)
    {
        using namespace toy3d;
        ActorFactory factory;
        MaterialAssignments materials;
        EditorSelection selection;
        SceneViewport viewport;
        World world;
        world.initialize();
        EditorCommandHistory history(factory);
        const auto id = history.place_actor(world, {});
        Actor* actor = world.find_actor_by_id(id);
        auto& light = actor->create_component<PointLightComponent>();
        selection.select_actor(world, id);
        history.mark_saved(world);
        std::string error;
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1200, 700);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        const auto frame = [&]()
        {
            ImGui::NewFrame();
            viewport.begin_frame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(600, 650));
            draw_details(world, selection, history, workspace, viewport, materials, error);
            const ImGuiID hovered = ImGui::GetCurrentContext()->HoveredId;
            ImGui::SetNextWindowPos(ImVec2(600, 0));
            ImGui::SetNextWindowSize(ImVec2(600, 650));
            viewport.draw(world, selection, history);
            ImGui::Render();
            return hovered;
        };
        frame();
        const int component_id = static_cast<int>(light.component_id());
        const ImGuiID scope = ImHashData(&component_id, sizeof(component_id), ImGui::FindWindowByName("Details")->ID);
        const ImGuiID intensity = ImHashStr("Intensity", 0, scope);
        float target_y = 0;
        // Discover the actual widget by identity rather than relying on font/layout coordinates.
        for (float y = 30; y < 600 && target_y == 0; y += 4)
        {
            io.AddMousePosEvent(100, y);
            if (frame() == intensity) target_y = y;
        }
        check(target_y != 0, "Actual registered light Intensity control is discoverable");
        if (target_y != 0)
        {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            check(history.active_for(EditorTransformSource::Details), "Viewport drawn after Details does not finish its activation");
            for (int step = 1; step <= 3; ++step)
            {
                io.AddMousePosEvent(100 + 20.0f * step, target_y);
                frame();
                check(history.active_for(EditorTransformSource::Details), "Details drag stays active across real UI frames");
            }
            const float edited = light.intensity();
            check(edited != 1 && error.empty(), "Continuous UI drag updates the runtime component");
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            check(!history.active() && history.undo(world) && light.intensity() == 1 && !history.dirty(world),
                  "Release commits one undoable gesture and restores the saved point");
            check(history.redo(world) && light.intensity() == edited, "Redo restores the complete UI drag");
            history.mark_saved(world);
            io.AddMousePosEvent(100, target_y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMousePosEvent(160, target_y);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            check(!history.active() && light.intensity() == edited && !history.dirty(world),
                  "Escape restores the starting value without an extra history record");
            io.AddKeyEvent(ImGuiKey_Escape, false);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
        }
        history.clear();
        ImGui::DestroyContext();
    }

    void check_texture_candidates(toy3d::EditorWorkspace& workspace)
    {
        using namespace toy3d;
        Texture2DAsset texture;
        texture.width = texture.height = 1;
        texture.format = PixelFormat::R8G8B8A8UNormSRGB;
        texture.mips.push_back({4, 4, {20, 40, 60, 255}});
        AssetId first;
        AssetId second;
        check(AssetId::try_generate(first) && AssetId::try_generate(second), "Texture candidate identities allocated");
        const auto a = encode_texture_asset_pair(workspace.types(), first, texture);
        const auto b = encode_texture_asset_pair(workspace.types(), second, texture);
        const auto path_a = virtual_path("/Project/preview_a.asset");
        const auto path_b = virtual_path("/Project/preview_b.asset");
        check(a.succeeded() && b.succeeded() &&
              workspace.asset_pairs().publish(path_a, a.value(), FilePublishMode::CreateNew).succeeded() &&
              workspace.asset_pairs().publish(path_b, b.value(), FilePublishMode::CreateNew).succeeded() && workspace.refresh(),
              "Texture candidate fixtures published");
        ThreadManager threads;
        auto created = create_task_graph({0, 16, false}, threads);
        check(created.succeeded(), "Deterministic texture task graph created");
        if (!created.succeeded()) return;
        auto graph = created.take_task_graph();
        check(graph->attach_to_thread(NamedThread::GameThread).succeeded(), "Texture owner attaches to GT");
        TexturePreviewPanel panel(workspace);
        const auto prepare = [&]()
        {
            panel.tick();
            graph->process_thread_until_idle(NamedThread::GameThread);
            panel.tick();
            UiRenderWork work;
            panel.collect_render_work(work);
            return work;
        };
        const auto complete = [&](const UiTextureUpload& upload, const char* error)
        {
            UiTextureResult result;
            result.request_id = upload.request_id;
            result.texture_id = upload.texture_id;
            result.extent = upload.extent;
            result.error = error;
            panel.on_texture_result(std::move(result));
        };
        panel.request_open(first);
        auto work = prepare();
        check(work.uploads.size() == 1 && !panel.asset_id().valid(), "Decoded asset waits for GPU success before promotion");
        if (work.uploads.size() != 1)
        {
            panel.shutdown();
            check(graph->shutdown(TaskGraphShutdownMode::Drain).succeeded(), "Failed fixture still drains its texture task graph");
            return;
        }
        complete(work.uploads.front(), "");
        const auto previous = panel.texture_ids();
        check(panel.asset_id() == first && previous.size() == 1, "First texture becomes the displayed preview");
        const auto meta_b = virtual_path("/Project/preview_b.meta");
        const auto disk = workspace.files().read_binary(meta_b);
        check(disk.succeeded() && workspace.files().write_binary(meta_b, {1}, FileWriteMode::Truncate).succeeded(),
              "Corrupt candidate fixture without changing its catalog identity");
        panel.request_open(second);
        work = prepare();
        check(work.uploads.empty() && work.retire_textures.empty() && panel.asset_id() == first &&
              panel.texture_ids() == previous && !panel.error().empty(), "CPU load failure preserves the old asset and image");
        check(workspace.files().write_binary(meta_b, disk.value(), FileWriteMode::Truncate).succeeded(), "Candidate bytes repaired");
        panel.request_open(second);
        work = prepare();
        check(work.uploads.size() == 1 && work.retire_textures.empty() && panel.asset_id() == first,
              "Preparing another texture does not retire the displayed image");
        if (work.uploads.size() == 1)
        {
            complete(work.uploads.front(), "Simulated upload failure");
            UiRenderWork failed;
            panel.collect_render_work(failed);
            check(failed.retire_textures == std::vector<ImGuiTextureId>{work.uploads.front().texture_id} &&
                  panel.asset_id() == first && panel.texture_ids() == previous, "GPU failure retires only its candidate");
        }
        panel.request_open(second);
        work = prepare();
        check(work.uploads.size() == 1, "Failed candidate can be retried");
        if (work.uploads.size() == 1)
        {
            complete(work.uploads.front(), "");
            UiRenderWork accepted;
            panel.collect_render_work(accepted);
            check(panel.asset_id() == second && accepted.retire_textures == previous && panel.error().empty(),
                  "Successful candidate replaces the image and retires the previous image once");
            const auto current = panel.texture_ids();
            panel.request_open(first);
            work = prepare();
            panel.request_open(second);
            if (work.uploads.size() == 1) complete(work.uploads.front(), "");
            UiRenderWork stale;
            panel.collect_render_work(stale);
            check(panel.asset_id() == second && panel.texture_ids() == current && stale.retire_textures.size() == 1,
                  "A late upload result cannot publish an abandoned asset request");
        }
        panel.shutdown();
        panel.shutdown();
        check(graph->shutdown(TaskGraphShutdownMode::Drain).succeeded(), "Texture task graph drains on shutdown");
    }
}

int main()
{
    using namespace toy3d;
    AssetId fixture_id;
    if (!AssetId::try_generate(fixture_id)) return 1;
    // C++17 filesystem only composes/cleans a unique test fixture; production I/O uses FileSystem.
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("toy3d_scene_framework_" + fixture_id.hex());
    NativePlatformFile platform;
    EditorWorkspacePaths paths;
    paths.project_assets = PhysicalPath((root / "project").u8string());
    paths.engine_assets = PhysicalPath((root / "engine").u8string());
    paths.editor_resources = PhysicalPath((root / "editor").u8string());
    paths.deployment = PhysicalPath((root / "bin").u8string());
    for (const auto& path : {paths.project_assets, paths.engine_assets, paths.editor_resources, paths.deployment})
        check(platform.create_directories(path).succeeded(), "Fixture directory exists");
    {
        EditorWorkspace workspace;
        if (!workspace.initialize(paths)) { std::cerr << workspace.error() << '\n'; return 1; }
        ActorFactory factory;
        MaterialAssignments materials;
        EditorSelection selection;
        SceneViewport viewport;
        World world;
        world.initialize();
        EditorSceneSession session(workspace, factory, materials, selection, viewport);
        session.bind(world);
        check(session.new_scene() && !session.dirty(), "Empty new scene is clean");
        PlacementRequest placement;
        const auto original_id = session.history().place_actor(world, placement);
        Actor* actor = world.find_actor_by_id(original_id);
        if (!actor) return 1;
        auto& point = actor->create_component<PointLightComponent>();
        auto& camera = actor->create_component<CameraComponent>();
        check(point.attach_to(actor->root_component(), AttachmentRule::KeepRelative) &&
              camera.attach_to(&point, AttachmentRule::KeepRelative), "Internal attachment graph is authored");
        check(point.set_intensity(4) && camera.set_perspective(75, 20, 50000), "Typed properties apply without RT binding");
        SceneAssetData saved;
        check(session.capture(saved) && saved.actors[0].components.size() == 3, "All owned components enter the scene snapshot");
        const auto actor_identity = saved.actors[0].id;
        const auto component_identity = saved.actors[0].components[1].id;
        const VirtualPath destination = virtual_path("/Project/multi.scene");
        check(session.save(destination, true) && !session.dirty(), "Scene publication establishes a saved history point");
        const AssetId asset_id = session.asset_id();
        const auto point_id = point.component_id();
        const auto transform = actor->root_component()->local_transform();
        SceneComponentData candidate;
        check(factory.component_editors().capture(point, candidate), "Typed component captures");
        // C++17 get selects the known point-light payload for atomic edit validation.
        auto& point_data = std::get<ScenePointLightData>(candidate.properties);
        point_data.light.intensity = 9;
        candidate.transform.translation.x = 37;
        session.history().begin(world, original_id, transform, EditorTransformSource::Details);
        point_data.attenuation.range = -1;
        check(!session.history().preview_component(world, original_id, point_id, candidate) && point.intensity() == 4 &&
              point.local_transform().translation.x == 0, "Invalid aggregate preserves all component fields");
        point_data.attenuation.range = 2500;
        check(session.history().preview_component(world, original_id, point_id, candidate), "Valid multi-field preview applies");
        check(!session.save(destination, false), "Active interaction cannot publish an intermediate scene");
        session.history().cancel();
        check(point.intensity() == 4 && point.local_transform().translation.x == 0 && !session.dirty(),
              "Cancel restores properties and saved-point state");
        session.history().begin(world, original_id, transform, EditorTransformSource::Details);
        check(session.history().preview_component(world, original_id, point_id, candidate), "First gesture preview");
        point_data.light.intensity = 12;
        check(session.history().preview_component(world, original_id, point_id, candidate), "Second preview stays in one gesture");
        session.history().finish(world, EditorTransformSource::Details);
        check(session.dirty() && session.history().undo(world) && point.intensity() == 4 && !session.dirty(),
              "One undo restores the whole gesture and saved point");
        check(session.history().redo(world) && point.intensity() == 12 && session.dirty(), "Redo restores non-root component properties");
        check(session.save(destination, false) && !session.dirty(), "Second saved history point is recorded");
        check(session.history().undo(world) && session.dirty(), "Undo before a saved point is dirty");
        session.history().begin(world, original_id, transform, EditorTransformSource::Details);
        point_data.light.intensity = 6;
        check(session.history().preview_component(world, original_id, point_id, candidate), "Branched gesture applies");
        session.history().finish(world, EditorTransformSource::Details);
        check(!session.history().redo(world) && session.dirty(), "Branch cannot collide with an abandoned saved revision");
        check(session.history().error().empty(), "An empty redo stack does not report an execution failure");
        check(session.open(asset_id) && !session.dirty() && world.actor_count() == 1, "Open replaces the whole scene and resets history");
        actor = world.find_actor_by_id(world.actor_ids().front());
        check(actor && actor->component_count() == 3, "Reconstruction has exactly the recorded components");
        auto* restored_point = dynamic_cast<PointLightComponent*>(actor->find_component_by_id(actor->component_ids()[1]));
        auto* restored_camera = dynamic_cast<CameraComponent*>(actor->find_component_by_id(actor->component_ids()[2]));
        check(restored_point && restored_camera && restored_point->intensity() == 12 &&
              restored_camera->vertical_fov_degrees() == 75 && restored_camera->parent() == restored_point &&
              restored_point->parent() == actor->root_component(), "Typed properties and complete attachment chain survive reopening");
        SceneAssetData reopened;
        check(session.capture(reopened) && reopened.actors[0].id == actor_identity &&
              reopened.actors[0].components[1].id == component_identity, "Persistent identity survives reopening");
        const auto current_id = actor->actor_id();
        SceneAssetData bad = reopened;
        bad.actors[0].components[0].type = "toy3d.UnknownComponent";
        check(!session.replace(bad) && world.find_actor_by_id(current_id) == actor && !session.dirty(),
              "Unknown schema fails before mutating the World");
        bad = reopened;
        bad.actors[0].components[0].parent_component_id = bad.actors[0].components[2].id;
        check(!session.replace(bad) && !session.dirty(), "Attachment cycle is rejected");
        bad = reopened;
        SceneActorData incompatible = reopened.actors[0];
        AssetId extra_actor;
        check(AssetId::try_generate(extra_actor), "Additional actor identity allocated");
        incompatible.id = extra_actor.hex();
        incompatible.kind = "DirectionalLight";
        for (auto& component : incompatible.components)
        {
            AssetId extra_component;
            check(AssetId::try_generate(extra_component), "Additional component identity allocated");
            component.id = extra_component.hex();
            component.parent_component_id.clear();
        }
        incompatible.root_component_id = incompatible.components[0].id;
        bad.actors.push_back(incompatible);
        check(!session.replace(bad) && world.actor_count() == 1 && world.find_actor_by_id(current_id) == actor && !session.dirty(),
              "Late construction failure rolls back candidates and preserves the saved point");
        PlacementRequest child_placement;
        const auto child_id = session.history().place_actor(world, child_placement);
        Actor* child = world.find_actor_by_id(child_id);
        check(child && child->root_component()->attach_to(restored_point, AttachmentRule::KeepRelative),
              "Cross-actor attachment targets a non-root component");
        SceneAssetData before_delete;
        check(session.capture(before_delete), "Capture before deletion");
        check(session.history().delete_actor(world, current_id) && child->root_component()->parent() == nullptr,
              "Deleting parent safely detaches its child");
        check(session.history().undo(world), "Undo deletion reconstructs all components");
        SceneAssetData after_delete_undo;
        check(session.capture(after_delete_undo) && after_delete_undo.actors.size() == 2, "Capture reconstructed scene");
        const auto parent = child->root_component()->parent();
        check(parent && parent->owner().actor_id() != current_id && dynamic_cast<const PointLightComponent*>(parent),
              "Child attachment follows reconstructed non-root component ID");
        const auto restored_actor = std::find_if(after_delete_undo.actors.begin(), after_delete_undo.actors.end(),
            [&](const SceneActorData& value) { return value.id == actor_identity; });
        check(restored_actor != after_delete_undo.actors.end() && restored_actor->components[1].id == component_identity,
              "Undo reconstruction preserves persistent component identity");
        check(session.save(destination, false) && !session.dirty(), "Attachment graph saves after undo");
        check(session.open(asset_id) && world.actor_count() == 2 && !session.dirty(), "Cross-actor graph reopens");
        Actor* modified = world.find_actor_by_id(world.actor_ids().front());
        Transform external = modified->root_component()->local_transform();
        external.translation.y += 10;
        check(modified->root_component()->set_local_transform(external) && session.dirty(),
              "External runtime setter marks author content dirty without a render proxy");
        session.history().synchronize(world);
        check(session.save(destination, false) && !session.dirty(), "Save acknowledges external author content");
        const auto revision = world.content_revision();
        check(modified->root_component()->set_local_transform(external) && world.content_revision() == revision,
              "Identical transform does not create a false author change");
        world.mark_scene_changed();
        check(!session.dirty(), "Render cache invalidation alone does not dirty author content");
        const auto disk = workspace.files().read_binary(destination);
        check(disk.succeeded(), "Published scene bytes readable");
        auto changed_disk = disk.value();
        changed_disk.push_back('\n');
        check(workspace.files().write_binary(destination, changed_disk, FileWriteMode::Truncate).succeeded(), "External file edit fixture");
        check(!session.save(destination, false), "External scene file edit produces a save conflict");
        check(workspace.files().write_binary(destination, disk.value(), FileWriteMode::Truncate).succeeded(), "Restore published bytes");
        session.history().clear();
        // Validate the checked-in demo through the current codec, without a runtime loader or compatibility parser.
        FileSystem demo_files;
        DirectoryFileStoreDesc demo_store;
        demo_store.physical_root = PhysicalPath(TOY3D_EDITOR_PROJECT_ASSET_ROOT);
        const auto store = DirectoryFileStore::create(platform, demo_store);
        check(store.succeeded(), "Demo asset store exists");
        FileMountDesc mount;
        mount.virtual_root = virtual_path("/Demo");
        mount.store = store.value();
        mount.access = MountAccess::ReadOnly;
        check(demo_files.add_mount(mount).succeeded() && demo_files.freeze().succeeded(), "Demo source is mounted read-only");
        SceneAssetData demo;
        check(read_scene_asset(workspace.types(), demo_files, virtual_path("/Demo/ShadowDemo.scene"), demo).succeeded() &&
              demo.actors.size() == 4, "Shadow demo is valid current component schema");
        std::vector<std::uint8_t> baseline;
        // Preserve external formatting too; re-encoding would silently discard this trailing newline.
        check(workspace.files().write_binary(destination, changed_disk, FileWriteMode::Truncate).succeeded(),
              "Scene baseline fixture includes original noncanonical whitespace");
        check(read_scene_asset(workspace.types(), workspace.files(), destination, demo, nullptr, &baseline).succeeded() &&
              baseline == changed_disk, "Scene decoder returns the exact original validated file bytes as its baseline");
        const auto old_baseline = baseline;
        check(!read_scene_asset(workspace.types(), workspace.files(), virtual_path("/Project/missing.scene"), demo,
                                nullptr, &baseline).succeeded() && baseline == old_baseline,
              "Failed scene read leaves the previous baseline untouched");
        check(workspace.files().write_binary(destination, disk.value(), FileWriteMode::Truncate).succeeded(),
              "Scene baseline fixture restored");
        check_details_drag(workspace);
        check_texture_candidates(workspace);
    }
    {
        EditorPanelRegistry panels;
        std::string order;
        int undo_a = 0;
        int undo_b = 0;
        bool focus_a = true;
        bool focus_b = false;
        EditorPanel a{"a", "A", "A", [&]() { order += 'a'; }, [&]() { ++undo_a; }, []() {}, [&]() { return focus_a; }};
        EditorPanel b{"b", "B", "B", [&]() { order += 'b'; }, [&]() { ++undo_b; }, []() {}, [&]() { return focus_b; }};
        check(panels.add(a) && !panels.add(a) && panels.add(b), "Panels reject duplicate stable identity");
        panels.freeze();
        check(!panels.add({"late", "Late", "Late", []() {}, {}, {}, {}}), "Frozen panel registry rejects late callback mutation");
        panels.draw(); panels.undo();
        check(order == "ab" && undo_a == 1 && undo_b == 0, "Registered order and focused history route are stable");
        focus_a = false; focus_b = true; panels.draw(); panels.undo();
        focus_b = false; panels.draw(); panels.undo();
        check(undo_b == 2 && undo_a == 1, "Menu focus preserves the last active document history");
        panels.clear(); panels.undo();
        check(undo_b == 2, "Shutdown clears borrowed UI callbacks");
        AssetEditorRegistry assets;
        bool opened = false;
        check(assets.add({"toy3d.TestData", [&](const AssetId& id, bool focus) { opened = id == fixture_id && focus; }}),
              "Asset editor registers by persistent root type");
        assets.freeze();
        check(!assets.add({"late", [](const AssetId&, bool) {}}) &&
              assets.request_open("toy3d.TestData", fixture_id, true) && opened &&
              !assets.request_open("toy3d.Unregistered", fixture_id, true), "Asset dispatch is frozen and rejects unknown types");
    }
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    check(!cleanup_error, "Unique fixture directory cleanup succeeds");
    std::cout << "Editor framework failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
