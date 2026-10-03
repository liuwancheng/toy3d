#include "application/application.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "platform/platform_defines.h"
#if WITH_WIN
#include <windows.h>
#include "platform/win/win32_window.h"
#endif

#include "assets/preview/mesh_editor_panel.h"
#include "assets/mesh/mesh_material_edit_session.h"
#include "assets/thumbnails/thumbnail_source.h"
#include "asset/mesh/mesh_materials.h"
#include "assets/thumbnails/asset_thumbnail_pool.h"
#include "config/command_line_parser.h"
#include "engine.h"
#include "gamescene/world/world.h"
#include "image/png_codec.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "panels/content_browser_panel.h"
#include "rendercore/frame_synchronization.h"
#include "scene/placement/actor_factory.h"
#include "scene/editor_scene_session.h"
#include "scene/editor_play_session.h"
#include "scene/mesh_asset_bindings.h"
#include "gamescene/actor/skeletal_mesh_actor.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "threading/task_graph/task_graph.h"
#include "threading/thread_manager.h"
#include "scene/editor_selection.h"
#include "workspace/editor_workspace.h"
#include "viewport/scene_viewport.h"

namespace
{
    using namespace toy3d;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    AssetId id_for(const EditorWorkspace& workspace, const std::string& name)
    {
        for (const auto& entry : workspace.catalog().entries)
        {
            if (entry.path.utf8() == "/Project/" + name + ".asset")
            {
                return entry.file.asset_id;
            }
        }
        throw std::runtime_error("Missing Manny test asset: " + name);
    }

    void check_scene_mesh_bindings(EditorWorkspace& workspace, const MeshPreviewAsset& asset)
    {
        ActorFactory factory;
        check(factory.initialize(), "Scene binding factory initialization failed.");
        {
            MaterialAssignments materials;
            EditorSelection selection;
            SceneViewport viewport;
            World world;
            world.initialize();
            EditorSceneSession scene(workspace, factory, materials, selection, viewport);
            scene.bind(world);
            PlacementRequest request;
            request.item = PlacementItemId::StaticMesh;
            const auto static_id = scene.history().place_actor(world, request);
            check(dynamic_cast<StaticMeshActor*>(world.find_actor_by_id(static_id)) != nullptr,
                  "Empty StaticMeshActor placement failed.");
            request.item = PlacementItemId::SkeletalMesh;
            const auto skeletal_id = scene.history().place_actor(world, request);
            auto* actor = dynamic_cast<SkeletalMeshActor*>(world.find_actor_by_id(skeletal_id));
            check(actor && !actor->skeletal_mesh_component().skeletal_mesh(),
                  "Empty SkeletalMeshActor placement failed.");
            auto& child = world.find_actor_by_id(static_id)->create_component<SkeletalMeshComponent>();
            check(child.attach_to(world.find_actor_by_id(static_id)->root_component(), AttachmentRule::KeepRelative),
                  "Non-root mesh attachment failed.");
            scene.history().mark_saved(world);
            ThreadManager threads;
            auto created = create_task_graph({0, 16, false}, threads);
            check(created.succeeded(), "Binding task graph creation failed.");
            auto tasks = created.take_task_graph();
            check(tasks->attach_to_thread(NamedThread::GameThread).succeeded(),
                  "Binding owner could not attach to GT.");
            MeshAssetBindings bindings(workspace, scene.history());
            bindings.initialize(*tasks);
            const auto complete = [&]()
            {
                tasks->process_thread_until_idle(NamedThread::GameThread);
                bindings.tick(world);
                check(!bindings.busy(), "Binding task did not complete.");
            };
            const auto component_id = actor->skeletal_mesh_component().component_id();
            const auto queued_revision = world.content_revision();
            check(bindings.request(world, skeletal_id, component_id, "mesh", asset.mesh_id) &&
                      !scene.history().dirty(world) && world.content_revision() == queued_revision,
                  "Queued load dirtied the Scene.");
            complete();
            check(bindings.error().empty() && actor->skeletal_mesh_component().skeletal_mesh(), bindings.error());
            check(bindings.request(world, skeletal_id, component_id, "animation", asset.sequence_id),
                  "Animation binding did not queue.");
            complete();
            auto& component = actor->skeletal_mesh_component();
            check(bindings.error().empty() && component.animation_sequence(), bindings.error());
            const auto previous_mesh = component.skeletal_mesh();
            const auto previous_sequence = component.animation_sequence();
            const auto before_failure = factory.capture(*actor);
            check(bindings.request(world, skeletal_id, component_id, "mesh", id_for(workspace, "SKM_Manny_Simple")),
                  "Incompatible mesh load did not queue.");
            complete();
            check(!bindings.error().empty() && same_actor_state(before_failure, factory.capture(*actor)),
                  "Incompatible mesh replaced the existing mesh/animation.");
            check(bindings.request(world, static_id, child.component_id(), "mesh", asset.mesh_id),
                  "Non-root mesh did not queue.");
            complete();
            check(bindings.error().empty() && child.skeletal_mesh(), bindings.error());
            check(component.seek(0.5).succeeded(), "Scene fixture seek failed.");
            auto state = factory.capture(*actor);
            auto& properties = std::get<SceneSkeletalMeshData>(state.components.front().data.properties);
            properties.playback = {false, false, 0.5};
            properties.lock_root = true;
            std::string error;
            check(scene.history().replace_mesh(world, skeletal_id, state.components.front(), error), error);
            SceneAssetData saved;
            check(scene.capture(saved), scene.error());
            const auto destination = VirtualPath::parse("/Project/AnimationBinding.scene").value();
            check(scene.save(destination, true) && !scene.dirty(), scene.error());
            check(component.seek(0.9).succeeded() && !scene.dirty(), "Playback seek dirtied author state.");
            const auto scene_id = scene.asset_id();
            check(bindings.request(world, skeletal_id, component_id, "mesh", {}), "Clear mesh failed.");
            check(!component.skeletal_mesh() && !component.animation_sequence(),
                  "Clearing mesh retained an animation.");
            check(scene.history().undo(world) && component.skeletal_mesh() == previous_mesh &&
                      component.animation_sequence() == previous_sequence && !scene.dirty(),
                  "Undo did not restore the immutable resource snapshot or saved point.");
            check(scene.history().redo(world) && !component.skeletal_mesh(), "Redo did not clear mesh resources.");
            check(scene.open(scene_id) && world.actor_count() == 2 && !scene.dirty(), scene.error());
            bool found = false;
            for (const auto id : world.actor_ids())
            {
                if (const auto* restored = dynamic_cast<const SkeletalMeshActor*>(world.find_actor_by_id(id)))
                {
                    const auto& mesh = restored->skeletal_mesh_component();
                    found = mesh.skeletal_mesh() && mesh.animation_sequence() && mesh.lock_root() &&
                            !mesh.playback_settings().loop && !mesh.playback_settings().autoplay &&
                            mesh.playback_settings().rate == 0.5 && mesh.playback_state()->time() == 0.0;
                }
            }
            check(found, "Scene reopen lost skeletal resources/settings or restored a transient animation time.");
            const auto unchanged_revision = world.content_revision();
            check(bindings.request(world, world.actor_ids().front(),
                                   world.find_actor_by_id(world.actor_ids().front())->root_component()->component_id(),
                                   "mesh", AssetId{}),
                  "Empty mesh clear failed.");
            check(!scene.dirty() && world.content_revision() == unchanged_revision,
                  "Clearing an already empty mesh changed author state.");
            bindings.shutdown();
            check(tasks->shutdown(TaskGraphShutdownMode::Drain).succeeded(), "Binding workers did not drain.");
            scene.history().clear();
            for (const auto id : world.actor_ids())
            {
                world.destroy_actor(*world.find_actor_by_id(id));
            }
        }
        factory.release();
    }

    AssetRef test_material_reference(const EditorWorkspace& workspace)
    {
        for (const auto& entry : workspace.catalog().entries)
        {
            if (entry.path.utf8() == "/Project/MeshMaterialsFixture.asset")
            {
                return {entry.file.asset_id, {}, entry.file.root_type, AssetRefStrength::Strong};
            }
        }
        throw std::runtime_error("Mesh material fixture is missing.");
    }

    void test_mesh_material_edits(EditorWorkspace& workspace)
    {
        VirtualPath source;
        for (const auto& entry : workspace.catalog().entries)
        {
            if (entry.path.utf8() == "/Engine/M_Default.asset" && entry.file.root_type == "toy3d.MaterialAssetData")
            {
                source = entry.path;
                break;
            }
        }
        const auto material_path = VirtualPath::parse("/Project/MeshMaterialsFixture.asset").value();
        const auto copied = workspace.asset_pairs().copy(source, material_path);
        check(copied.succeeded() && workspace.refresh(), "Create isolated material fixture.");
        const auto reference = test_material_reference(workspace);
        const auto extra_path = VirtualPath::parse("/Project/MeshMaterialsDependency.asset").value();
        const auto extra = workspace.asset_pairs().copy(source, extra_path);
        check(extra.succeeded() && workspace.refresh(), "Create unrelated material dependency fixture.");
        const auto extra_pair = workspace.asset_pairs().read(extra_path);
        check(extra_pair.succeeded(), "Read unrelated material dependency fixture.");
        const AssetRef unrelated{
            extra_pair.value().description.index.asset_id, {}, "toy3d.MaterialAssetData", AssetRefStrength::Strong};
        for (const char* name : {"spider", "SKM_Manny"})
        {
            const auto id = id_for(workspace, name);
            const auto path = workspace.catalog().index.find(id)->path;
            const auto original = workspace.asset_pairs().read(path);
            check(original.succeeded(), "Read mesh material edit baseline.");
            const auto original_meta = encode_asset_meta(original.value().meta);
            auto extended_index = original.value().description.index;
            extended_index.dependencies.push_back(unrelated);
            auto extended_segments = original.value().meta.segments;
            extended_segments.push_back({"editor_test_extension", 2, false, {1, 2, 3, 4}});
            const auto extended = encode_asset_pair(workspace.types(), extended_index,
                                                    original.value().description.type_data, extended_segments);
            check(extended.succeeded(), "Encode opaque metadata fixture: " + extended.status().message);
            const auto attached = workspace.asset_pairs().publish(path, extended.value(), FilePublishMode::Replace);
            check(attached.succeeded(), "Attach opaque metadata fixture: " + attached.message);
            check(workspace.refresh(), "Refresh opaque metadata fixture: " + workspace.error());
            MeshMaterialEditSession edit(workspace);
            bool reject = false;
            const auto prepare = [&reject](const std::vector<AssetRef>&)
            {
                return reject ? AssetStatus{AssetErrorCode::Value, {}, {}, {}, {}, "Injected material rejection.", {}}
                              : AssetStatus::success();
            };
            check(edit.open(id, prepare).succeeded() && edit.writable() && !edit.dirty(),
                  "Legacy mesh opens as a clean material draft.");
            check(workspace.asset_pairs().read(path).value().description_bytes == extended.value().asset,
                  "Opening a legacy mesh does not rewrite source bytes.");
            reject = true;
            check(!edit.set_material(0, reference).succeeded() && !edit.dirty(),
                  "Failed preview preparation leaves mesh materials unchanged.");
            reject = false;
            AssetRef wrong = reference;
            wrong.expected_type = "toy3d.Texture2DAssetData";
            check(!edit.set_material(0, wrong).succeeded() && !edit.dirty(), "Wrong material type is rejected.");
            check(edit.set_material(0, reference).succeeded() && edit.dirty(),
                  "Mesh material assignment creates a draft.");
            check(edit.undo().succeeded() && !edit.dirty() && edit.redo().succeeded() && edit.dirty(),
                  "Mesh material undo and redo preserve the saved point.");
            check(edit.save().succeeded() && !edit.dirty(), "Save mesh default material.");
            const auto saved = workspace.asset_pairs().read(path);
            check(saved.succeeded() && encode_asset_meta(saved.value().meta).value() == extended.value().meta &&
                      std::any_of(saved.value().description.index.dependencies.begin(),
                                  saved.value().description.index.dependencies.end(),
                                  [&unrelated](const AssetRef& value)
                                  {
                                      return value.asset_id == unrelated.asset_id;
                                  }),
                  "Changing mesh materials preserves geometry and opaque meta bytes.");
            MeshMaterialEditSession reopened(workspace);
            check(reopened.open(id, prepare).succeeded() && reopened.materials()[0].asset_id == reference.asset_id,
                  "Reopening restores the saved default material.");
            check(workspace.catalog().index.resolve(reference, "saved mesh material").succeeded() &&
                      std::any_of(saved.value().description.index.dependencies.begin(),
                                  saved.value().description.index.dependencies.end(),
                                  [&reference](const AssetRef& value)
                                  {
                                      return value.asset_id == reference.asset_id;
                                  }),
                  "Mesh default material is a declared strong dependency.");
            if (std::string(name) == "spider")
            {
                const auto entry = std::find_if(workspace.catalog().entries.begin(), workspace.catalog().entries.end(),
                                                [&id](const AssetCatalogEntry& value)
                                                {
                                                    return value.file.asset_id == id;
                                                });
                check(entry != workspace.catalog().entries.end(), "Static mesh remains in the isolated catalog.");
                const auto first = load_thumbnail_source(workspace.files(), workspace.asset_pairs(),
                                                         workspace.catalog(), *entry, true);
                check(first.succeeded() && first.value().skeletal->sources.size() > 1,
                      "Static thumbnail captures its material dependencies.");
                const auto material_original = workspace.files().read_binary(material_path);
                auto changed = material_original.value();
                changed.push_back('\n');
                check(
                    workspace.files().write_binary_atomic(material_path, changed, FilePublishMode::Replace).succeeded(),
                    "Change isolated material source baseline.");
                const auto second = load_thumbnail_source(workspace.files(), workspace.asset_pairs(),
                                                          workspace.catalog(), *entry, true);
                check(second.succeeded() && first.value().source.content_hash != second.value().source.content_hash &&
                          !mesh_preview_asset_current(workspace.asset_pairs(), workspace.catalog(),
                                                      *first.value().skeletal),
                      "A material dependency change invalidates the mesh thumbnail source and prior snapshot.");
                check(workspace.files()
                          .write_binary_atomic(material_path, material_original.value(), FilePublishMode::Replace)
                          .succeeded(),
                      "Restore isolated material source.");
            }
            check(reopened.set_material(0, {}).succeeded(), "Clear mesh material to the engine fallback.");
            auto changed = saved.value().description_bytes;
            changed.push_back('\n');
            check(workspace.files().write_binary_atomic(path, changed, FilePublishMode::Replace).succeeded() &&
                      reopened.save().code == AssetErrorCode::Conflict && reopened.dirty(),
                  "External mesh edits reject save and retain the draft.");
            reopened.clear();
            edit.clear();
            check(workspace.asset_pairs()
                          .publish(path, {original.value().description_bytes, original_meta.value(), true},
                                   FilePublishMode::Replace)
                          .succeeded() &&
                      workspace.refresh(),
                  "Restore isolated mesh fixture after material edits.");
        }
        const auto remapped = remap_mesh_materials({"A", "B"}, {reference, {}}, {"B", "A"});
        check(remapped.succeeded() && remapped.value()[1].asset_id == reference.asset_id &&
                  !remap_mesh_materials({"A"}, {reference}, {"B"}).succeeded() &&
                  !remap_mesh_materials({"A"}, {reference}, {"A", "A"}).succeeded(),
              "Reimport remaps materials by unique slot names and rejects lost/ambiguous assignments.");
    }

    struct TestState
    {
        bool complete = false;
        std::string error;
    };

    // --------------------------------------------------------------------------
    // AnimationPreviewApplication: production preview and thumbnail coexistence
    // --------------------------------------------------------------------------
    class AnimationPreviewApplication final : public Application
    {
      public:
        AnimationPreviewApplication(EditorWorkspace& workspace, TestState& state)
            : workspace_(workspace), panel_(workspace), pool_(workspace), state_(state),
              skeleton_(id_for(workspace, "SKM_Manny_Skeleton")), simple_(id_for(workspace, "SKM_Manny_Simple")),
              full_(id_for(workspace, "SKM_Manny")), run_(id_for(workspace, "MM_Run_Fwd")),
              idle_(id_for(workspace, "MM_Idle")), static_(id_for(workspace, "spider"))
        {
        }

      private:
        bool on_initialize() override
        {
            ImGui::GetIO().IniFilename = nullptr;
            author_revision_ = world().content_revision();
            if (!factory_.initialize())
            {
                return false;
            }
            assigned_material_ = MaterialInstance::create(factory_.default_material());
            check(assigned_material_ && assigned_material_->set_vector("base_color", vec4(0.05f, 0.85f, 0.12f, 1)),
                  "Create an owned green material for GPU slot assignment.");
            const MeshMaterialResolver resolver = [this](const AssetRef&)
            {
                return AssetResult<MaterialInterfaceRef>(assigned_material_);
            };
            panel_.set_material_resolver(resolver);
            pool_.set_material_resolver(resolver);
            factory_.set_material_resolver(resolver);
            return true;
        }
        bool starts_world_play() const override
        {
            return false;
        }
        bool uses_preview_scene() const override
        {
            return true;
        }
        bool uses_play_scene() const override
        {
            return true;
        }
        void on_initialize_play_scene(SceneInterface& scene) override
        {
            play_scene_ = &scene;
        }
        bool renders_play_scene() const override
        {
            return play_.active();
        }
        std::shared_ptr<SceneRenderFeedback> scene_render_feedback() const override
        {
            return play_.feedback();
        }
        bool uses_animation_preview_scene() const override
        {
            return true;
        }
        bool on_initialize_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks) override
        {
            if (!pool_.initialize(scene, factory_.default_material(), tasks))
            {
                return false;
            }
            // Exercise reference-pose GPU generation even when a prior run left valid PNGs.
            pool_.generate(simple_);
            pool_.generate(full_);
            pool_.generate(run_);
            pool_.generate(idle_);
            return true;
        }
        bool on_initialize_animation_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks) override
        {
            if (!panel_.initialize(scene, factory_.default_material(), tasks))
            {
                state_.error = panel_.error();
                return false;
            }
            panel_.request_open(skeleton_);
            return true;
        }
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override
        {
            views.emplace_back(Vector3(0, 0, -300), Quaternion::identity(), Vector3(0, 0, 1),
                               IntRect{0, 0, extent.width, extent.height}, extent, CameraProjectionMode::Perspective,
                               Radians(k_pi / 4), 1.0f, 10000.0f);
        }
        void fail(const std::string& error)
        {
            state_.error = error;
            window().close();
        }
        void on_tick(double delta) override
        {
            if (phase_ != timed_phase_)
            {
                timed_phase_ = phase_;
                elapsed_ = 0;
                std::cout << "Animation preview phase " << phase_ << std::endl;
            }
            else
            {
                elapsed_ += delta;
            }
            // Bound each operation, rather than charging repeated Debug asset validation
            // to the final reopen. CTest also bounds the complete integration run.
            if (elapsed_ > 90)
            {
                if (phase_ == 8)
                {
                    for (const auto& entry : workspace_.catalog().entries)
                    {
                        if (entry.file.asset_id == simple_ || entry.file.asset_id == full_ ||
                            entry.file.asset_id == run_ || entry.file.asset_id == idle_)
                        {
                            const auto thumbnail = pool_.request(entry);
                            std::cout << "Pending thumbnail " << entry.path.utf8()
                                      << " texture=" << thumbnail.texture_id.value() << " busy=" << thumbnail.busy
                                      << " error=" << thumbnail.error << std::endl;
                        }
                    }
                }
                fail("Animation preview timed out at phase " + std::to_string(phase_) + ": " + panel_.error());
                return;
            }
            pool_.tick();
            panel_.tick(delta);
            if (phase_ == 26)
            {
                // Exercise the production canvas across real ImGui frames; no private
                // camera access or OS input timing is needed to prove orbit behavior.
                auto& io = ImGui::GetIO();
                if (orbit_frame_ == 0)
                {
                    const auto* root = ImGui::FindWindowByName("Animation Editor###MeshEditor");
                    check(root != nullptr, "Animation window exists for orbit input");
                    orbit_window_position_ = root->Pos;
                    bool found = false;
                    for (const auto* child : ImGui::GetCurrentContext()->Windows)
                    {
                        if (std::string(child->Name).find("/Animation viewport_") != std::string::npos)
                        {
                            orbit_position_ = child->InnerRect.GetCenter();
                            found = true;
                            break;
                        }
                    }
                    check(found, "Production animation canvas exists for orbit input");
                    io.AddMousePosEvent(orbit_position_.x, orbit_position_.y);
                }
                else if (orbit_frame_ == 1)
                {
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                }
                else if (orbit_frame_ == 2)
                {
                    io.AddMousePosEvent(orbit_position_.x + 80.0f, orbit_position_.y);
                }
                else if (orbit_frame_ == 3)
                {
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                }
                ++orbit_frame_;
            }
            if (phase_ == 24 && !panel_.error().empty())
            {
                const auto ids = panel_.texture_ids();
                if (panel_.error().find("Preview environment:") == std::string::npos || ids.size() != 1 ||
                    ids.front() != retained_preview_texture_ || !panel_.asset() || !(panel_.asset()->id == full_))
                {
                    fail("Invalid preview environment replaced the active asset or image.");
                    return;
                }
                panel_.set_preview_scene_settings(PreviewSceneSettings{});
                phase_ = 25;
            }
            if (phase_ == 5 && !panel_.error().empty())
            {
                if (!panel_.asset() || !(panel_.asset()->id == idle_))
                {
                    fail("Failed candidate replaced the active animation.");
                    return;
                }
                panel_.close();
                panel_.request_open(skeleton_);
                panel_.tick(0);
                panel_.close();
                panel_.request_open(full_);
                phase_ = 6;
            }
            if (phase_ == 20)
            {
                play_.tick(delta);
                if (!play_.active())
                {
                    fail("Skeletal PIE: " + play_.error());
                    return;
                }
                if (play_.state() == EditorPlayState::Playing && ++play_frames_ >= 3)
                {
                    bool animated = false;
                    for (const auto id : play_.world()->actor_ids())
                    {
                        const auto* actor = dynamic_cast<const SkeletalMeshActor*>(play_.world()->find_actor_by_id(id));
                        if (actor)
                        {
                            const auto& mesh = actor->skeletal_mesh_component();
                            animated =
                                mesh.has_render_state() && mesh.playback_state() && mesh.playback_state()->time() > 0.0;
                        }
                    }
                    if (!animated || !world().actor_ids().empty() || world().content_revision() != author_revision_)
                    {
                        fail("Skeletal PIE did not animate independently from the author World.");
                        return;
                    }
                    play_.stop();
                    state_.complete = true;
                    window().close();
                }
                return;
            }
            if (phase_ == 8)
            {
                bool ready = true;
                std::size_t ready_count = 0;
                for (const auto& entry : workspace_.catalog().entries)
                {
                    if (entry.file.asset_id == simple_ || entry.file.asset_id == full_ || entry.file.asset_id == run_ ||
                        entry.file.asset_id == idle_)
                    {
                        const auto thumbnail = pool_.request(entry);
                        if (!thumbnail.error.empty())
                        {
                            fail(thumbnail.error);
                            return;
                        }
                        const bool complete = thumbnail.texture_id.valid() && !thumbnail.busy;
                        ready = ready && complete;
                        ready_count += complete ? 1u : 0u;
                    }
                }
                if (ready_count > thumbnail_ready_count_)
                {
                    // Four thumbnails are serialized by the production pool. Bound the wait for the next
                    // completed thumbnail; CTest separately bounds the complete integration run.
                    thumbnail_ready_count_ = ready_count;
                    elapsed_ = 0;
                    std::cout << "Animation thumbnail progress " << ready_count << "/4" << std::endl;
                }
                if (ready)
                {
                    std::vector<Sha256Hash> hashes;
                    for (const auto& entry : workspace_.catalog().entries)
                    {
                        if (entry.file.asset_id == full_ || entry.file.asset_id == run_ || entry.file.asset_id == idle_)
                        {
                            const auto image = pool_.request(entry);
                            const auto found = thumbnail_hashes_.find(image.texture_id.value());
                            if (found == thumbnail_hashes_.end())
                            {
                                fail("Animation thumbnail did not come from a GPU render.");
                                return;
                            }
                            hashes.push_back(found->second);
                        }
                    }
                    if (hashes.size() != 3 || hashes[0] == hashes[1] || hashes[0] == hashes[2] ||
                        hashes[1] == hashes[2])
                    {
                        fail("Reference pose, Run first frame and Idle first frame have identical thumbnail pixels.");
                        return;
                    }
                    if (!world().actor_ids().empty() || world().content_revision() != author_revision_)
                    {
                        fail("Preview changed the author World.");
                        return;
                    }
                    SceneAssetData snapshot;
                    const auto path = VirtualPath::parse("/Project/AnimationBinding.scene");
                    const auto read = read_scene_asset(workspace_.types(), workspace_.files(), path.value(), snapshot,
                                                       &workspace_.catalog().index);
                    if (!read.succeeded())
                    {
                        fail("Skeletal PIE source: " + read.message);
                        return;
                    }
                    for (auto& actor : snapshot.actors)
                    {
                        for (auto& component : actor.components)
                        {
                            if (auto* mesh = std::get_if<SceneSkeletalMeshData>(&component.properties))
                            {
                                mesh->playback.autoplay = true;
                                mesh->playback.rate = 1.0;
                            }
                        }
                    }
                    const auto resolver = [this](const std::string& name,
                                                 const std::vector<shader::ShaderPermutationSelection>& selections)
                    {
                        const auto& map = factory_.default_material()->material()->desc().shader_map;
                        return name == factory_.default_material()->material()->desc().shader_name && selections.empty()
                                   ? map
                                   : nullptr;
                    };
                    if (!play_scene_ ||
                        !play_.start(snapshot, workspace_, factory_.actor_types(), resolver, *play_scene_))
                    {
                        fail("Skeletal PIE initialization: " + play_.error());
                        return;
                    }
                    phase_ = 20;
                }
            }
        }
        void on_build_ui() override
        {
            const auto size = ImGui::GetIO().DisplaySize;
            if (phase_ != 26)
            {
                ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            }
#if WITH_WIN
            const ImVec2 preview_size(size.x, std::max(360.0f, size.y - 190));
#else
            // Other platforms still exercise preview-target reconstruction. Native
            // WSI resize is only driven by the owned Win32 window in this fixture.
            const ImVec2 preview_size(phase_ >= 7 ? size.x * 0.75f : size.x,
                                      std::max(360.0f, size.y - (phase_ >= 7 ? 280.0f : 190.0f)));
#endif
            // The production panel supplies a FirstUseEver default size; resize its
            // existing window explicitly so the native resize also changes its canvas.
            ImGui::SetWindowSize("Animation Editor###MeshEditor", preview_size, ImGuiCond_Always);
            ImGui::SetNextWindowSize(preview_size, ImGuiCond_Always);
            panel_.draw();
            ImGui::SetNextWindowPos(ImVec2(0, size.y - 185), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(size.x, 185), ImGuiCond_Always);
            browser_.draw(workspace_, selection_, folder_, show_engine_, pool_);
        }
        void on_collect_ui_render_work(UiRenderWork& work) override
        {
            pool_.collect_render_work(work);
            panel_.collect_render_work(work);
        }
        std::vector<ImGuiTextureId> ui_texture_ids() const override
        {
            auto ids = panel_.texture_ids();
            const auto thumbnails = pool_.texture_ids();
            ids.insert(ids.end(), thumbnails.begin(), thumbnails.end());
            return ids;
        }
        void capture(const UiTextureResult& result)
        {
            Rgba8Image image;
            image.width = result.extent.width;
            image.height = result.extent.height;
            image.pixels = result.bgra_pixels;
            for (std::size_t i = 0; i < image.pixels.size(); i += 4)
            {
                std::swap(image.pixels[i], image.pixels[i + 2]);
            }
            std::vector<std::uint8_t> png;
            NativePlatformFile files;
            const std::string path =
                std::string(TOY3D_ANIMATION_PREVIEW_TEST_ROOT) + "/phase-" + std::to_string(phase_) + ".png";
            if (!encode_png(image, png).succeeded() ||
                !files.write_binary(PhysicalPath(path), png, FileWriteMode::Truncate).succeeded())
            {
                fail("Animation preview capture failed.");
            }
        }
        void on_ui_texture_result(UiTextureResult result) override
        {
            if (result.texture_id.value() < (1ull << 44))
            {
                if (result.succeeded() && !result.bgra_pixels.empty())
                {
                    thumbnail_hashes_[result.texture_id.value()] = sha256(result.bgra_pixels);
                }
                pool_.on_texture_result(std::move(result));
                return;
            }
            panel_.on_texture_result(result);
            if (!result.succeeded())
            {
                fail("GPU preview: " + result.error);
                return;
            }
            if (!panel_.asset() ||
                result.bgra_pixels.size() != static_cast<std::size_t>(result.extent.width) * result.extent.height * 4)
            {
                fail("Preview completed before a valid asset or GPU image.");
                return;
            }
            capture(result);
            const auto hash = sha256(result.bgra_pixels);
            switch (phase_)
            {
            case 0:
            {
                bool visible = false;
                for (std::size_t pixel = 0; pixel < result.bgra_pixels.size(); pixel += 4)
                {
                    visible = visible || result.bgra_pixels[pixel] > 100 || result.bgra_pixels[pixel + 1] > 100 ||
                              result.bgra_pixels[pixel + 2] > 100;
                }
                if (panel_.asset()->mesh || panel_.asset()->sequence || !visible)
                {
                    fail("Skeleton-only preview has no visible joints.");
                    return;
                }
                panel_.request_open(simple_);
                phase_ = 1;
                break;
            }
            case 1:
                if (!panel_.asset()->mesh || panel_.asset()->layout->skeleton().bones.size() != 89)
                {
                    fail("Simple mesh preview did not preserve its 89-bone skeleton.");
                    return;
                }
                panel_.set_preview_display(true, false, false);
                panel_.request_open(run_);
                phase_ = 2;
                break;
            case 2:
                if (!panel_.asset()->sequence || !panel_.asset()->mesh)
                {
                    fail("Animation did not select its compatible mesh.");
                    return;
                }
                pose_hash_ = hash;
                retained_mesh_ = panel_.asset()->mesh;
                panel_.seek(0.6);
                phase_ = 3;
                break;
            case 3:
                if (hash == pose_hash_)
                {
                    fail("Seeking the sequence did not change the GPUSkin image.");
                    return;
                }
                panel_.set_preview_display(true, true, true);
                phase_ = 4;
                break;
            case 4:
                panel_.set_preview_display(false, true, false);
                panel_.set_playing(true);
                phase_ = 9;
                break;
            case 9:
            {
                panel_.set_playing(false);
                panel_.set_preview_display(true, true, false);
                panel_.request_open(idle_);
                phase_ = 10;
                break;
            }
            case 10:
                check(panel_.asset()->id == idle_ && panel_.asset()->mesh == retained_mesh_,
                      "Switching animation did not reuse the immutable mesh.");
                panel_.request_open(run_);
                phase_ = 13;
                break;
            case 13:
                check(panel_.asset()->id == run_ && panel_.asset()->mesh == retained_mesh_,
                      "Visible mesh replacement did not preserve the immutable mesh.");
                panel_.close();
                panel_.request_open(idle_);
                phase_ = 11;
                break;
            case 11:
                check(panel_.asset()->id == idle_ && panel_.asset()->mesh == retained_mesh_,
                      "Closing/reopening lost the last successful CPU mesh.");
                panel_.invalidate();
                phase_ = 12;
                break;
            case 12:
            {
                check(panel_.asset()->id == idle_ && panel_.asset()->mesh != retained_mesh_,
                      "Invalidation reused the revoked CPU mesh.");
                AssetId missing;
                AssetId::try_generate(missing);
                panel_.request_open(missing);
                phase_ = 5;
                break;
            }
            case 6:
                if (!(panel_.asset()->id == full_))
                {
                    fail("A stale closed-session load replaced the reopened mesh.");
                    return;
                }
                panel_.set_preview_display(true, true, false);
                old_extent_ = result.extent;
#if WITH_WIN
                if (auto* native = dynamic_cast<Win32Window*>(&window()))
                {
                    SetWindowPos(native->get_native_hwnd(), nullptr, 0, 0, 950, 780,
                                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
                }
#endif
                phase_ = 7;
                break;
            case 7:
                if (result.extent.width == old_extent_.width && result.extent.height == old_extent_.height)
                {
                    return;
                }
                pose_hash_ = hash;
                {
                    auto settings = panel_.preview_scene_settings();
                    check(settings == MaterialPreviewSettings{}.scene,
                          "Animation and material previews use the same default scene.");
                    settings.show_environment = false;
                    check(panel_.set_preview_scene_settings(settings), "Hide animation environment background");
                }
                phase_ = 21;
                break;
            case 21:
                check(hash != pose_hash_, "Background visibility changes the real animation preview image");
                pose_hash_ = hash;
                {
                    auto settings = panel_.preview_scene_settings();
                    settings.exposure_ev = 1;
                    panel_.set_preview_scene_settings(settings);
                }
                phase_ = 22;
                break;
            case 22:
                check(hash != pose_hash_, "Exposure changes the real animation preview image");
                pose_hash_ = hash;
                {
                    auto settings = panel_.preview_scene_settings();
                    settings.show_floor = false;
                    settings.show_shadows = false;
                    panel_.set_preview_scene_settings(settings);
                }
                phase_ = 23;
                break;
            case 23:
                check(hash != pose_hash_, "Floor visibility changes the real animation preview image");
                retained_preview_texture_ = result.texture_id;
                {
                    auto settings = panel_.preview_scene_settings();
                    settings.exposure_ev = std::numeric_limits<float>::quiet_NaN();
                    check(!panel_.set_preview_scene_settings(settings), "Reject non-finite preview settings");
                    settings = panel_.preview_scene_settings();
                    AssetId::try_generate(settings.environment);
                    panel_.set_preview_scene_settings(settings);
                }
                phase_ = 24;
                break;
            case 25:
                check(panel_.preview_scene_settings() == PreviewSceneSettings{},
                      "Restore the common scene after failure");
                pose_hash_ = hash;
                phase_ = 26;
                break;
            case 26:
            {
                check(hash != pose_hash_, "Dragging the production viewport rotates the GPU preview");
                const auto* root = ImGui::FindWindowByName("Animation Editor###MeshEditor");
                check(root && root->Pos.x == orbit_window_position_.x && root->Pos.y == orbit_window_position_.y,
                      "Orbit input does not move the animation editor window");
                ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                panel_.request_open(static_);
                phase_ = 27;
                break;
            }
            case 27:
                check(panel_.asset()->id == static_ && panel_.asset()->static_mesh && !panel_.asset()->layout &&
                          !panel_.asset()->mesh && !panel_.asset()->sequence,
                      "Static mesh preview must not require a Skeleton or Animation.");
                check(panel_.asset()->static_mesh->indices.size() == 3936 &&
                          panel_.asset()->static_mesh->material_slots.size() == 4,
                      "Spider preview preserves imported triangles and material slots.");
                retained_static_ = panel_.asset()->static_mesh;
                pose_hash_ = hash;
                panel_.set_preview_display(false, false, false);
                phase_ = 31;
                break;
            case 31:
                check(hash != pose_hash_, "Static geometry contributes visible pixels to the GPU image.");
                panel_.set_preview_display(true, false, false);
                phase_ = 32;
                break;
            case 32:
                pose_hash_ = hash;
                {
                    auto settings = panel_.preview_scene_settings();
                    settings.show_environment = false;
                    panel_.set_preview_scene_settings(settings);
                }
                phase_ = 28;
                break;
            case 28:
                check(hash != pose_hash_, "Static preview uses the common environment controls.");
                pose_hash_ = hash;
                {
                    auto settings = panel_.preview_scene_settings();
                    settings.show_floor = false;
                    panel_.set_preview_scene_settings(settings);
                }
                phase_ = 33;
                break;
            case 33:
                check(hash != pose_hash_, "Static preview floor follows the real mesh bounds.");
                panel_.close();
                panel_.request_open(static_);
                phase_ = 29;
                break;
            case 29:
                check(panel_.asset()->static_mesh == retained_static_,
                      "Static close/reopen reuses CPU geometry and recreates live render data.");
                pose_hash_ = hash;
                for (std::size_t i = 0; i < panel_.material_edit_session().slots().size(); ++i)
                {
                    check(panel_.assign_material(i, test_material_reference(workspace_)).succeeded(),
                          "Assign StaticMesh default material through the production edit session.");
                }
                phase_ = 34;
                break;
            case 34:
                check(hash != pose_hash_, "StaticMesh material assignment changes real GPU pixels.");
                pose_hash_ = hash;
                for (std::size_t i = 0; i < panel_.material_edit_session().slots().size(); ++i)
                {
                    check(panel_.undo_material().succeeded(), "Undo StaticMesh material assignment.");
                }
                phase_ = 35;
                break;
            case 35:
                check(hash != pose_hash_, "StaticMesh material undo changes rendered slot pixels.");
                for (std::size_t i = 0; i < panel_.material_edit_session().slots().size(); ++i)
                {
                    check(panel_.redo_material().succeeded(), "Redo StaticMesh material assignment.");
                }
                check(panel_.save_materials().succeeded(), "Save StaticMesh materials.");
                phase_ = 36;
                break;
            case 36:
                check(!panel_.material_edit_session().dirty() &&
                          panel_.asset()->static_mesh->default_materials[0].asset_id ==
                              test_material_reference(workspace_).asset_id,
                      "Saved StaticMesh materials reload through the normal paired snapshot path.");
                panel_.close();
                panel_.request_open(static_);
                phase_ = 37;
                break;
            case 37:
                check(panel_.material_edit_session().materials()[0].asset_id ==
                          test_material_reference(workspace_).asset_id,
                      "StaticMesh close/reopen retains the assigned material.");
                panel_.request_open(full_);
                phase_ = 30;
                break;
            case 30:
                check(panel_.asset()->id == full_ && panel_.asset()->mesh && !panel_.asset()->static_mesh,
                      "Switching back from static to skeletal mesh recreates the correct scene.");
                pose_hash_ = hash;
                for (std::size_t i = 0; i < panel_.material_edit_session().slots().size(); ++i)
                {
                    check(panel_.assign_material(i, test_material_reference(workspace_)).succeeded(),
                          "Assign SkeletalMesh default material through the same edit session.");
                }
                phase_ = 38;
                break;
            case 38:
                check(hash != pose_hash_, "GPUSkin default material assignment changes real GPU pixels.");
                pose_hash_ = hash;
                for (std::size_t i = 0; i < panel_.material_edit_session().slots().size(); ++i)
                {
                    check(panel_.undo_material().succeeded(), "Undo SkeletalMesh default material.");
                }
                phase_ = 39;
                break;
            case 39:
                check(hash != pose_hash_, "GPUSkin material undo changes rendered slot pixels.");
                for (std::size_t i = 0; i < panel_.material_edit_session().slots().size(); ++i)
                {
                    check(panel_.redo_material().succeeded(), "Redo SkeletalMesh default material.");
                }
                check(panel_.save_materials().succeeded(), "Save SkeletalMesh default materials.");
                phase_ = 40;
                break;
            case 40:
                check(!panel_.material_edit_session().dirty() && !panel_.asset()->mesh->data.default_materials.empty(),
                      "SkeletalMesh material defaults survive paired snapshot reload.");
                panel_.request_open(idle_);
                phase_ = 41;
                break;
            case 41:
                check(panel_.asset()->sequence && !panel_.material_edit_session().active() &&
                          !panel_.asset()->mesh->data.default_materials.empty(),
                      "Animation preview uses saved mesh materials without creating an animation material draft.");
                pool_.invalidate();
                phase_ = 8;
                break;
            default:
                break;
            }
        }
        void on_shutdown() override
        {
            play_.stop();
            panel_.shutdown();
            pool_.shutdown();
            MaterialInstance::release(assigned_material_);
            if (!flush_rendering_commands().succeeded())
            {
                state_.error = "Animation preview teardown did not drain.";
            }
            factory_.release();
        }

        EditorWorkspace& workspace_;
        MeshEditorPanel panel_;
        AssetThumbnailPool pool_;
        ActorFactory factory_;
        MaterialInstanceRef assigned_material_;
        EditorPlaySession play_;
        SceneInterface* play_scene_ = nullptr;
        int play_frames_ = 0;
        ContentBrowserPanel browser_;
        EditorSelection selection_;
        TestState& state_;
        AssetId skeleton_, simple_, full_, run_, idle_, static_;
        std::shared_ptr<const StaticMeshAssetGeometry> retained_static_;
        std::shared_ptr<const SkeletalMeshAsset> retained_mesh_;
        std::string folder_ = "/Project";
        bool show_engine_ = false;
        int phase_ = 0;
        ImGuiTextureId retained_preview_texture_;
        int timed_phase_ = -1;
        double elapsed_ = 0;
        std::size_t thumbnail_ready_count_ = 0;
        std::uint64_t author_revision_ = 0;
        Sha256Hash pose_hash_{};
        std::map<std::uint64_t, Sha256Hash> thumbnail_hashes_;
        Extent old_extent_;
        int orbit_frame_ = 0;
        ImVec2 orbit_position_;
        ImVec2 orbit_window_position_;
    };
} // namespace

int main(int argc, char** argv)
{
    using namespace toy3d;
    try
    {
        // C++17 filesystem copies fixtures into an isolated writable asset root;
        // the user's Project files are never candidates for test mutation.
        const std::filesystem::path root(TOY3D_ANIMATION_PREVIEW_TEST_ROOT);
        const auto assets = root / "asset";
        std::filesystem::create_directories(assets);
        // Only this test's generated Scene pair is reset; imported fixtures and caches remain reusable.
        std::filesystem::remove(assets / "AnimationBinding.scene");
        std::filesystem::remove(assets / "AnimationBinding.scene.meta");
        std::filesystem::remove(assets / "MeshMaterialsFixture.asset");
        std::filesystem::remove(assets / "MeshMaterialsFixture.meta");
        std::filesystem::remove(assets / "MeshMaterialsDependency.asset");
        std::filesystem::remove(assets / "MeshMaterialsDependency.meta");
        for (const auto& source : std::filesystem::directory_iterator(TOY3D_MANNY_PREVIEW_SOURCE))
        {
            std::filesystem::copy_file(source.path(), assets / source.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing);
        }
        for (const char* name : {"spider.asset", "spider.meta"})
        {
            std::filesystem::copy_file(std::filesystem::path(TOY3D_STATIC_PREVIEW_SOURCE) / name, assets / name,
                                       std::filesystem::copy_options::overwrite_existing);
        }
        EditorWorkspacePaths paths;
        paths.project_assets = PhysicalPath(assets.u8string());
        paths.saved = PhysicalPath((root / "saved").u8string());
        paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
        paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
        paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
        EditorWorkspace workspace;
        check(workspace.initialize(paths), workspace.error());
        test_mesh_material_edits(workspace);
        const auto static_id = id_for(workspace, "spider");
        const auto static_preview = load_mesh_preview_asset(workspace.asset_pairs(), workspace.catalog(), static_id);
        check(static_preview.succeeded() && static_preview.value().static_mesh && !static_preview.value().layout,
              "Load a static mesh without skeleton dependencies.");
        const auto static_cached = std::make_shared<const MeshPreviewAsset>(static_preview.value());
        const auto static_reused = load_mesh_preview_asset(workspace.asset_pairs(), workspace.catalog(), static_id,
                                                           false, {}, {}, static_cached);
        check(static_reused.succeeded() && static_reused.value().static_mesh == static_cached->static_mesh &&
                  mesh_preview_asset_current(workspace.asset_pairs(), workspace.catalog(), *static_cached),
              "Reuse unchanged static geometry from a validated paired snapshot.");
        const auto* static_location = workspace.catalog().index.find(static_id);
        const auto static_original = workspace.files().read_binary(static_location->path);
        auto static_changed = static_original.value();
        static_changed.push_back('\n');
        check(workspace.files()
                      .write_binary_atomic(static_location->path, static_changed, FilePublishMode::Replace)
                      .succeeded() &&
                  !mesh_preview_asset_current(workspace.asset_pairs(), workspace.catalog(), *static_cached),
              "A changed static descriptor revokes its preview baseline.");
        check(workspace.files()
                  .write_binary_atomic(static_location->path, static_original.value(), FilePublishMode::Replace)
                  .succeeded(),
              "Restore isolated static mesh fixture.");
        const auto simple = id_for(workspace, "SKM_Manny_Simple");
        const auto run = id_for(workspace, "MM_Run_Fwd");
        const auto skeleton = id_for(workspace, "SKM_Manny_Skeleton");
        auto loaded = load_mesh_preview_asset(workspace.asset_pairs(), workspace.catalog(), run);
        check(loaded.succeeded(), loaded.status().message);
        const auto cached = std::make_shared<const MeshPreviewAsset>(std::move(loaded).value());
        const auto idle = id_for(workspace, "MM_Idle");
        const auto switched =
            load_mesh_preview_asset(workspace.asset_pairs(), workspace.catalog(), idle, false, {}, {}, cached);
        check(switched.succeeded() && switched.value().mesh == cached->mesh &&
                  switched.value().layout == cached->layout,
              "Unchanged compatible inputs did not reuse CPU mesh/layout.");
        check(
            !load_mesh_preview_asset(workspace.asset_pairs(), workspace.catalog(), run, true, simple, run).succeeded(),
            "Mixed full/Simple skeleton selection was accepted.");
        check(mesh_preview_asset_current(workspace.asset_pairs(), workspace.catalog(), *cached),
              "Valid preview baseline rejected.");
        const auto* location = workspace.catalog().index.find(skeleton);
        const auto original = workspace.files().read_binary(location->path);
        auto changed = original.value();
        changed.push_back('\n');
        check(workspace.files().write_binary_atomic(location->path, changed, FilePublishMode::Replace).succeeded(),
              "Baseline mutation failed.");
        check(!mesh_preview_asset_current(workspace.asset_pairs(), workspace.catalog(), *cached),
              "Changed Skeleton baseline accepted.");
        const auto refreshed =
            load_mesh_preview_asset(workspace.asset_pairs(), workspace.catalog(), run, false, {}, {}, cached);
        check(refreshed.succeeded() && refreshed.value().layout != cached->layout &&
                  refreshed.value().mesh != cached->mesh,
              "Changed Skeleton reused a stale mesh/layout.");
        check(workspace.files()
                  .write_binary_atomic(location->path, original.value(), FilePublishMode::Replace)
                  .succeeded(),
              "Baseline restore failed.");
        check_scene_mesh_bindings(workspace, *cached);
        check(set_animation_preview_mesh_preference(workspace.files(), run, simple).succeeded(),
              "Preview settings save failed.");
        check(!load_mesh_preview_asset(workspace.asset_pairs(), workspace.catalog(), run, false, {}, {}, {},
                                       &workspace.files())
                   .succeeded(),
              "Explicit incompatible preview preference silently fell back.");
        check(set_animation_preview_mesh_preference(workspace.files(), run, {}).succeeded(),
              "Preview preference clear failed.");
        const bool multi = argc > 1 && std::string(argv[1]) == "--multithread";
        CommandLineParser::get_instance().parser_args(
            {"AnimationPreviewTests", "--Window.Width=1200", "--Window.Height=900",
             "--Window.Title=Animation Preview Tests",
             multi ? "--Renderer.MultiThreaded=true" : "--Renderer.MultiThreaded=false"});
        TestState state;
        Engine engine;
        EngineStartupPaths startup;
        startup.saved = PhysicalPath((root / "runtime_saved").u8string());
        check(engine.set_startup_paths(std::move(startup)), "Test runtime Saved configuration failed.");
        engine.set_application(std::make_unique<AnimationPreviewApplication>(workspace, state));
#if WITH_WIN
        engine.init(static_cast<void*>(GetModuleHandleW(nullptr)));
#else
        engine.init(nullptr);
#endif
        check(engine.initialized(), "Animation preview Engine initialization failed.");
        engine.main_loop();
        engine.exit();
        check(state.complete && state.error.empty(), "Animation preview integration: " + state.error);
        std::cout << "Skeleton/Simple/full mesh, GPUSkin seek, depth lines, independent thumbnails, stale "
                     "close/reopen, failure retention and resize passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
