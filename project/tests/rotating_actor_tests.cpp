#include "rotating_actor.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

#include "application/game_host.h"
#include "asset/asset_yaml.h"
#include "file_system/native_platform_file.h"
#include "gamescene/scene_assembly.h"
#include "gamescene/world/world.h"
#include "scene/editor_scene_session.h"
#include "scene/actor_details.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "scene/editor_selection.h"
#include "viewport/scene_viewport.h"
#include "workspace/editor_workspace.h"

#if defined(TOY3D_TEST_SHADER_ROOT)
bool check_editor_play(toy3d::EditorWorkspace& workspace, const toy3d::ActorTypeRegistry& actors,
                       const toy3d::SceneAssetData& authored);
#endif

namespace
{
    int failures = 0;
    void check(bool value, const char* message)
    {
        if (!value)
        {
            ++failures;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    toy3d::Actor& borrow_actor(toy3d::World& world)
    {
        return *world.find_actor_by_id(world.actor_ids().front());
    }

    toy3d::Quaternion spin(int steps)
    {
        using namespace toy3d;
        World world;
        auto& actor = world.spawn_actor<RotatingActor>();
        auto& root = actor.create_component<SceneComponent>();
        check(actor.set_root_component(&root), "Root composition");
        Transform before;
        before.translation = Vector3(1, 2, 3);
        before.scale = Vector3(2, 3, 4);
        root.set_local_transform(before);
        world.initialize();
        world.tick(1.0);
        check(root.local_transform().rotation == before.rotation, "Editor World does not spin before BeginPlay");
        world.begin_play();
        for (int frame = 0; frame < steps; ++frame)
        {
            check(world.tick(2.0 / steps), "Gameplay tick succeeds");
        }
        check(root.local_transform().translation == before.translation && root.local_transform().scale == before.scale,
              "Rotation preserves translation and nonuniform scale");
        const auto rotation = root.local_transform().rotation;
        const auto forward = rotate_vector(rotation, Vector3(0, 0, 1));
        check(std::abs(forward.x - 1.0f) < 0.0001f && std::abs(forward.z) < 0.0001f,
              "Two seconds at 45 degrees per second rotates 90 degrees");
        RotationSettings settings = actor.rotation_settings();
        settings.enabled = false;
        check(actor.set_rotation_settings(settings) && world.tick(2.0) && root.local_transform().rotation == rotation,
              "Disabled rotation is stable");
        settings.axis = Vector3{};
        check(!actor.set_rotation_settings(settings) && !actor.rotation_settings().enabled,
              "Zero axis rejects without publishing");
        settings.axis = Vector3(0, 1, 0);
        settings.speed_degrees_per_second = std::numeric_limits<float>::quiet_NaN();
        check(!actor.set_rotation_settings(settings), "NaN speed rejects");
        settings.enabled = true;
        settings.speed_degrees_per_second = -45;
        check(actor.set_rotation_settings(settings) && world.tick(2.0), "Negative speed is supported");
        const auto reverse = rotate_vector(root.local_transform().rotation, Vector3(0, 0, 1));
        check(std::abs(reverse.z - 1.0f) < 0.0001f, "Negative spin returns to initial orientation");
        world.end_play();
        const auto stopped = root.local_transform().rotation;
        world.tick(1.0);
        check(root.local_transform().rotation == stopped, "Stopped World does not rotate");
        return rotation;
    }

    void check_scene_and_history()
    {
        using namespace toy3d;
        TypeRegistry metadata;
        ActorTypeRegistry actors;
        const auto module = linked_game_module();
        TypeDesc references;
        references.name = "test.ActorReferences";
        references.schema_version = 1;
        PropertyDesc links;
        links.name = "links";
        links.cpp_type = "std::vector<AssetRef>";
        links.value_type = {ValueKind::Array, {}, {{ValueKind::AssetRef, {}, {}}}};
        references.properties.push_back(links);
        check(metadata.add(references).succeeded(), "Asset reference payload schema registers");
        check(register_scene_asset_types(metadata).succeeded() && module.register_types(metadata, actors) &&
                  metadata.freeze().succeeded() && actors.freeze(metadata),
              "Module registers before freeze");
        if (!metadata.frozen() || !actors.frozen())
        {
            return;
        }
        World world;
        world.initialize();
        auto& actor = world.spawn_actor<RotatingActor>();
        auto& root = actor.create_component<SceneComponent>();
        actor.set_root_component(&root);
        ReflectedValue payload;
        check(actors.capture(actor, payload), "Typed actor payload captures");
        auto invalid = payload;
        invalid.schema_version = 2;
        check(!actors.apply(actor, invalid), "Unknown property version rejects");
        invalid = payload;
        invalid.bytes.push_back(0);
        check(!actors.apply(actor, invalid), "Trailing property bytes reject");
        SceneActorData saved;
        saved.id = "9bf3a6c28c9d403897b7062c62a7affb";
        saved.kind = "Custom";
        saved.type = "shadow_demo.RotatingActor";
        saved.properties = payload;
        saved.root_component_id = "0e607a83b9034e008a8dc1266760c6d7";
        SceneComponentData component;
        component.id = saved.root_component_id;
        component.type = "toy3d.SceneComponent";
        component.properties = SceneNodeData{};
        saved.components.push_back(component);
        SceneAssetData scene;
        scene.actors.push_back(saved);
        AssetId asset;
        AssetId::try_generate(asset);
        const auto encoded = encode_scene_asset_pair(metadata, asset, scene);
        check(encoded.succeeded(), "Scene7 dynamic payload encodes");
        if (!encoded.succeeded())
        {
            return;
        }
        const auto decoded = decode_asset_yaml(metadata, encoded.value().asset);
        SceneAssetData roundtrip;
        if (decoded.succeeded())
        {
            ValueReader reader(decoded.value().type_data);
            check(decoded.value().index.schema_version == 7u && decode_value(reader, roundtrip).succeeded() &&
                      reader.at_end(),
                  "Scene7 codec roundtrip");
        }
        check(decoded.succeeded() && roundtrip.actors.size() == 1 && roundtrip.actors.front().type == saved.type &&
                  roundtrip.actors.front().properties.bytes == payload.bytes,
              "Actor type and settings survive YAML");
        AssetId referenced;
        AssetId::try_generate(referenced);
        AssetRef weak{referenced, {}, "toy3d.SceneAssetData", AssetRefStrength::Weak};
        AssetRef strong = weak;
        strong.strength = AssetRefStrength::Strong;
        ValueWriter links_writer;
        links_writer.write_array_length(2);
        encode_value(links_writer, weak);
        encode_value(links_writer, strong);
        SchemaFields fields;
        fields["links"] = {true, links_writer.bytes()};
        ReflectedValue refs;
        refs.type = references.name;
        refs.schema_version = 1;
        encode_schema_fields(fields, refs.bytes);
        const auto extracted = reflected_value_references(metadata, refs);
        check(extracted.succeeded() && extracted.value().size() == 2,
              "Dynamic owned properties enumerate nested AssetRefs");
        auto referenced_scene = scene;
        referenced_scene.actors.front().properties = refs;
        const auto linked_bytes = encode_scene_asset_pair(metadata, asset, referenced_scene);
        check(linked_bytes.succeeded(), "Actor references encode into Scene dependencies");
        if (linked_bytes.succeeded())
        {
            const auto linked_yaml = decode_asset_yaml(metadata, linked_bytes.value().asset);
            check(linked_yaml.succeeded() && linked_yaml.value().index.dependencies.size() == 1 &&
                      linked_yaml.value().index.dependencies.front().strength == AssetRefStrength::Strong,
                  "Strong Actor reference wins when dependency IDs merge");
        }
        SceneAssemblyResult assembled;
        std::string error;
        check(assemble_scene(world, roundtrip, actors, metadata, {}, assembled, error) && world.actor_count() == 1,
              "Runtime restores project class");
        const auto original = world.actor_ids().front();
        check(dynamic_cast<RotatingActor*>(world.find_actor_by_id(original)) != nullptr,
              "Runtime uses registered concrete class");
        auto bad = roundtrip;
        bad.actors.front().type = "missing.Actor";
        check(!assemble_scene(world, bad, actors, metadata, {}, assembled, error) && world.find_actor_by_id(original),
              "Unknown type preserves old World");
        bad = roundtrip;
        bad.actors.push_back(saved);
        bad.actors.back().id = "1bf3a6c28c9d403897b7062c62a7affb";
        bad.actors.back().components.front().id = "1e607a83b9034e008a8dc1266760c6d7";
        bad.actors.back().root_component_id = bad.actors.back().components.front().id;
        bad.actors.back().type = "toy3d.DirectionalLightActor";
        bad.actors.back().properties = {"toy3d.ActorSettings", 1, {0, 0, 0, 0}};
        check(!assemble_scene(world, bad, actors, metadata, {}, assembled, error) && world.actor_count() == 1 &&
                  world.find_actor_by_id(original),
              "Constructor mismatch rolls back candidate actors");

        ActorTypeRegistry broken;
        ActorType wrong = *actors.find("shadow_demo.RotatingActor");
        wrong.name = "test.BorrowedActor";
        wrong.create = borrow_actor;
        check(broken.add(wrong) && broken.freeze(metadata), "Faulty factory test registration");
        bad = roundtrip;
        bad.actors.front().type = wrong.name;
        check(!assemble_scene(world, bad, broken, metadata, {}, assembled, error) && world.actor_count() == 1 &&
                  world.find_actor_by_id(original),
              "A factory returning an old Actor cannot adopt/delete it");

        ActorFactory factory;
        factory.actor_types() = actors;
        EditorCommandHistory history(factory);
        auto* restored = dynamic_cast<RotatingActor*>(world.find_actor_by_id(original));
        PlacementRequest placement;
        placement.actor_type = saved.type;
        factory.remember(*restored, placement);
        history.mark_saved(world);
        history.begin(world, original, restored->root_component()->local_transform(), EditorTransformSource::Details);
        RotationSettings settings = restored->rotation_settings();
        settings.speed_degrees_per_second = 123;
        World scratch;
        auto& probe = scratch.spawn_actor<RotatingActor>();
        probe.set_rotation_settings(settings);
        actors.capture(probe, payload);
        check(history.preview_actor_properties(world, original, payload), "Details publishes valid candidate");
        history.finish(world, EditorTransformSource::Details);
        check(history.dirty(world) && history.undo(world) &&
                  restored->rotation_settings().speed_degrees_per_second == 45 && !history.dirty(world),
              "Undo restores settings and saved point");
        check(history.redo(world) && restored->rotation_settings().speed_degrees_per_second == 123,
              "Redo restores settings");
        check(history.delete_actor(world, original) && world.actor_count() == 0 && history.undo(world),
              "Delete/undo restores project actor");
        restored = dynamic_cast<RotatingActor*>(world.find_actor_by_id(world.actor_ids().front()));
        check(restored && restored->rotation_settings().speed_degrees_per_second == 123 && !restored->has_begun_play(),
              "Delete undo retains concrete type/properties without starting play");

        {
            ActorFactory placement_factory;
            placement_factory.actor_types() = actors;
            check(placement_factory.initialize(), "Project placement geometry initializes");
            World placed;
            placed.initialize();
            EditorCommandHistory placement_history(placement_factory);
            PlacementRequest request;
            request.actor_type = "shadow_demo.RotatingActor";
            const auto id = placement_history.place_actor(placed, request);
            check(id != 0 && dynamic_cast<RotatingActor*>(placed.find_actor_by_id(id)),
                  "Project placement creates concrete Actor with default mesh");
            if (id)
            {
                check(placement_history.undo(placed) && placed.actor_count() == 0 && placement_history.redo(placed) &&
                          dynamic_cast<RotatingActor*>(placed.find_actor_by_id(placed.actor_ids().front())),
                      "Project placement undo/redo");
            }
            placement_history.clear();
            for (const auto owned : placed.actor_ids())
            {
                placed.destroy_actor(*placed.find_actor_by_id(owned));
            }
            placement_factory.release();
        }

        NativePlatformFile platform;
        const auto fixture = platform.read_binary(PhysicalPath(TOY3D_TEST_ENGINE_ASSETS "/Scenes/Default.scene"));
        check(fixture.succeeded(), "Current built-in scene fixture reads");
        if (!fixture.succeeded())
        {
            return;
        }
        const auto current = decode_asset_yaml(metadata, fixture.value());
        check(current.succeeded() && current.value().index.schema_version == 7u, "Built-in fixture uses Scene7");
        if (current.succeeded())
        {
            SceneAssetData data;
            ValueReader reader(current.value().type_data);
            check(decode_value(reader, data).succeeded() && reader.at_end() && !data.actors.empty() &&
                      data.actors.front().id == saved.id &&
                      data.actors.front().root_component_id == saved.root_component_id &&
                      data.actors.front().type == "toy3d.StaticMeshActor",
                  "Current fixture retains actor/component identities and explicit concrete type");
        }
        const std::string yaml(encoded.value().asset.begin(), encoded.value().asset.end());
        const std::string marker = "schema_version: 7";
        const auto marker_position = yaml.find(marker);
        check(marker_position != std::string::npos, "Current scene carries an explicit schema version");
        if (marker_position != std::string::npos)
        {
            for (const auto old_version : {5u, 6u})
            {
                auto old_yaml = yaml;
                old_yaml.replace(marker_position, marker.size(), "schema_version: " + std::to_string(old_version));
                const std::vector<std::uint8_t> old_bytes(old_yaml.begin(), old_yaml.end());
                check(!decode_asset_yaml(metadata, old_bytes).succeeded(), "Old scene schemas require offline rebuild");
            }
        }
    }
    void check_actor_widgets(toy3d::World& world, toy3d::RotatingActor& actor, toy3d::EditorCommandHistory& history,
                             const toy3d::TypeRegistry& types)
    {
        using namespace toy3d;
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(700, 500);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        std::string error;
        const auto frame = [&]()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(650, 400));
            ImGui::Begin("Actor Properties");
            draw_actor_details(actor, history, types, error);
            ImGui::End();
            const auto hovered = ImGui::GetCurrentContext()->HoveredId;
            ImGui::Render();
            return hovered;
        };
        frame();
        const char* label = "speed_degrees_per_second";
        const auto scope = ImHashStr(label, 0, ImGui::FindWindowByName("Actor Properties")->ID);
        const auto control = ImHashStr(label, 0, scope);
        float target_y = 0;
        for (float y = 25; y < 200 && target_y == 0; y += 3)
        {
            io.AddMousePosEvent(100, y);
            if (frame() == control)
            {
                target_y = y;
            }
        }
        check(target_y != 0, "Registered Actor speed widget is present");
        if (target_y != 0)
        {
            const auto speed = actor.rotation_settings().speed_degrees_per_second;
            history.mark_saved(world);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            check(history.active_for(EditorTransformSource::Details), "Actor widget starts shared history gesture");
            io.AddMousePosEvent(160, target_y);
            frame();
            const auto edited = actor.rotation_settings().speed_degrees_per_second;
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            check(edited != speed && error.empty() && !history.active() && history.undo(world) &&
                      actor.rotation_settings().speed_degrees_per_second == speed && !history.dirty(world),
                  "Actor speed drag commits one undoable gesture");
            check(history.redo(world) && actor.rotation_settings().speed_degrees_per_second == edited,
                  "Widget gesture redo");
            history.mark_saved(world);
            io.AddMousePosEvent(100, target_y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMousePosEvent(180, target_y);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            check(!history.active() && actor.rotation_settings().speed_degrees_per_second == edited &&
                      !history.dirty(world),
                  "Escape cancels Actor property gesture");
            io.AddKeyEvent(ImGuiKey_Escape, false);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
        }
        ImGui::DestroyContext();
    }

    void check_editor_persistence()
    {
        using namespace toy3d;
        AssetId fixture;
        check(AssetId::try_generate(fixture), "Fixture identity");
        // C++17 filesystem creates an isolated integration fixture; no source asset is written.
        const auto root = std::filesystem::temp_directory_path() / ("toy3d-rotation-" + fixture.hex());
        for (const char* directory : {"asset", "saved", "deploy"})
        {
            std::filesystem::create_directories(root / directory);
        }
        {
            ActorTypeRegistry actors;
            const auto module = linked_game_module();
            EditorWorkspace workspace;
            EditorWorkspacePaths paths;
            paths.project_assets = PhysicalPath((root / "asset").u8string());
            paths.saved = PhysicalPath((root / "saved").u8string());
            paths.deployment = PhysicalPath((root / "deploy").u8string());
            paths.engine_assets = PhysicalPath(TOY3D_TEST_ENGINE_ASSETS);
            paths.editor_resources = PhysicalPath(TOY3D_TEST_EDITOR_RESOURCES);
            check(workspace.initialize(paths,
                                       [&](TypeRegistry& types)
                                       {
                                           return module.register_types(types, actors);
                                       }) &&
                      actors.freeze(workspace.types()),
                  "Workspace accepts project module before scanning");
            if (workspace.ready())
            {
                ActorFactory factory;
                factory.actor_types() = actors;
                MaterialAssignments materials;
                EditorSelection selection;
                SceneViewport viewport;
                World world;
                world.initialize();
                EditorSceneSession session(workspace, factory, materials, selection, viewport);
                session.bind(world);
                check(session.new_scene(), "Create author scene");
                auto& actor = world.spawn_actor<RotatingActor>();
                actor.set_root_component(&actor.create_component<SceneComponent>());
                RotationSettings settings;
                settings.speed_degrees_per_second = 81;
                settings.axis = Vector3(1, 2, 3);
                actor.set_rotation_settings(settings);
                PlacementRequest request;
                request.actor_type = "shadow_demo.RotatingActor";
                factory.remember(actor, request);
                const auto path = VirtualPath::parse("/Project/Spin.scene").value();
                check(session.save(path, true) && !session.dirty(), "Save custom class and parameters");
                const auto bytes = workspace.files().read_binary(path);
                check(session.new_scene() && world.actor_count() == 0 && session.open_path(path.utf8()),
                      "Reopen saved project scene");
                auto* restored = dynamic_cast<RotatingActor*>(world.find_actor_by_id(world.actor_ids().front()));
                check(restored && restored->rotation_settings().axis == settings.axis &&
                          restored->rotation_settings().speed_degrees_per_second == 81 && !session.dirty(),
                      "Save/Open retains exact project settings and clean state");
                if (restored)
                {
                    check_actor_widgets(world, *restored, session.history(), workspace.types());
                    check(session.open_path(path.utf8()), "Reload after widget fixture");
                    restored = dynamic_cast<RotatingActor*>(world.find_actor_by_id(world.actor_ids().front()));
                    session.history().begin(world, restored->actor_id(), restored->root_component()->local_transform(),
                                            EditorTransformSource::Details);
                    settings.speed_degrees_per_second = 91;
                    restored->set_rotation_settings(settings);
                    session.history().finish(world, EditorTransformSource::Details);
                    const auto revision = world.content_revision();
                    const auto identity = session.asset_id();
                    SceneAssetData snapshot;
                    check(session.capture(snapshot), "Capture Play snapshot");
#if defined(TOY3D_TEST_SHADER_ROOT)
                    check(check_editor_play(workspace, actors, snapshot), "Isolated PIE lifecycle and native Actor");
#endif
                    AssetId play_id;
                    AssetId::try_generate(play_id);
                    const auto encoded =
                        encode_scene_asset_pair(workspace.types(), play_id, snapshot, &workspace.catalog().index);
                    const auto play_path = VirtualPath::parse("/Saved/play/test.scene").value();
                    workspace.files().create_directories(VirtualPath::parse("/Saved/play").value());
                    check(encoded.succeeded() && workspace.asset_pairs()
                                                     .publish(play_path, encoded.value(), FilePublishMode::CreateNew)
                                                     .succeeded(),
                          "Play snapshot publishes into Saved");
                    check(session.dirty() && world.content_revision() == revision && session.asset_id() == identity &&
                              session.path() == path && workspace.files().read_binary(path).value() == bytes.value(),
                          "Play snapshot retains author revision/identity/path/dirty/file");
                    check(session.history().undo(world) &&
                              restored->rotation_settings().speed_degrees_per_second == 81 && !session.dirty(),
                          "Play capture preserves author undo timeline");
                }
            }
        }
        std::filesystem::remove_all(root);
    }

} // namespace

int main()
{
    const auto slow = spin(60), fast = spin(240);
    check(std::abs(slow.x - fast.x) + std::abs(slow.y - fast.y) + std::abs(slow.z - fast.z) +
                  std::abs(slow.w - fast.w) <
              0.0001f,
          "Rotation is independent of frame rate");
    check_scene_and_history();
    check_editor_persistence();
    return failures ? 1 : 0;
}
