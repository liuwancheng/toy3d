#include "application/application.h"

#include <iostream>
#include <memory>
#include <string>

#include "imgui.h"
#include "platform/platform_defines.h"

#if WITH_WIN
#include <Windows.h>
#endif
#include "config/command_line_parser.h"
#include "engine.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/world/world.h"
#include "scene/material_assignments.h"
#include "scene/editor_command_history.h"
#include "shader/shader_workflow.h"
#include "assets/material/material_editor_panel.h"
#include "scene/placement/actor_factory.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/render_command.h"
#include "rendercore/geometry/static_mesh_render_data.h"
#include "workspace/editor_workspace.h"

namespace
{
    using namespace toy3d;
    struct TestState
    {
        bool complete = false;
        std::string error;
    };

    // --------------------------------------------------------------------------
    // RecordingProcesses: real compiler execution with isolated VS Code launch capture
    // --------------------------------------------------------------------------
    class RecordingProcesses final : public ProcessService
    {
      public:
        ProcessResult run(const PhysicalPath& executable, const std::vector<std::string>& arguments,
                          const ProcessRunOptions& options) const override
        {
            return native_.run(executable, arguments, options);
        }
        ProcessResult launch_detached(const PhysicalPath& executable,
                                      const std::vector<std::string>& arguments) const override
        {
            opened = executable;
            parameters = arguments;
            return {true, 0, {}};
        }
        mutable PhysicalPath opened;
        mutable std::vector<std::string> parameters;

      private:
        NativeProcessService native_;
    };

    // --------------------------------------------------------------------------
    // ShaderTestApplication: real Vulkan candidate validation, parameter and slot publication
    // --------------------------------------------------------------------------
    class ShaderTestApplication final : public Application
    {
      public:
        ShaderTestApplication(EditorWorkspace& workspace, ShaderWorkflowPaths paths, PhysicalPath source,
                              std::string text, TestState& state)
            : workspace_(workspace), paths_(std::move(paths)), source_(std::move(source)), text_(std::move(text)),
              state_(state)
        {
        }

      private:
        bool on_initialize() override
        {
            ImGui::GetIO().IniFilename = nullptr;
            if (!factory_.initialize())
            {
                return false;
            }
            const auto defaults = factory_.default_material()->material();
            MaterialTextureValues textures;
            for (const auto& resource : defaults->parameter_schema().resources)
            {
                const auto found = defaults->desc().texture_defaults.find(resource.parameter_id);
                if (found != defaults->desc().texture_defaults.end())
                {
                    textures.named_defaults[resource.default_value] = found->second;
                }
            }
            library_ = std::make_unique<MaterialLibrary>(
                workspace_.types(), workspace_.files(),
                [this]() -> const AssetIndex&
                {
                    return workspace_.catalog().index;
                },
                [this](const std::string& name)
                {
                    return shaders_.program(name);
                },
                std::move(textures));
            library_->set_default_material(defaults);
            library_->set_shader_diagnostic(
                [this](const std::string& name)
                {
                    return shaders_.unavailable_reason(name);
                });
            materials_.initialize(workspace_, *library_);
            materials_.set_shader_workflow(shaders_);
            panel_.initialize(workspace_, factory_.default_material()->material(),
                              PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
            std::string error;
            if (!shaders_.initialize(paths_, factory_.default_material()->material(), error))
            {
                state_.error = error;
                return false;
            }

            panel_.set_shader_workflow(shaders_);
            if (!shaders_.open_source("Project/Surface/Painted", 12u, 3u) || processes_.parameters.size() != 3u ||
                processes_.parameters[2].find(":12:3") == std::string::npos ||
                processes_.opened != paths_.code_executable)
            {
                state_.error = "VS Code did not receive the registered author source/line arguments.";
                return false;
            }
            PlacementRequest placement;
            placement.item = PlacementItemId::Cube;
            auto* actor = dynamic_cast<StaticMeshActor*>(factory_.create(world(), placement));
            if (!actor)
            {
                return false;
            }
            actor_id_ = actor->actor_id();
            return true;
        }
        bool starts_world_play() const override
        {
            return false;
        }
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override
        {
            views.emplace_back(Vector3(0, 0, -3), Quaternion::identity(), Vector3(0, 0, 1),
                               IntRect{0, 0, extent.width, extent.height}, extent, CameraProjectionMode::Perspective,
                               Radians(0.785398163f), 0.1f, 100.0f);
        }
        void on_collect_material_validation(std::vector<MaterialProgramValidationRef>& requests) override
        {
            shaders_.collect_validation(requests);
            if (restored_)
            {
                restored_->collect_validation(requests);
            }
        }
        void on_collect_builtin_shader_updates(std::vector<BuiltinShaderUpdateRef>& requests) override
        {
            shaders_.collect_builtin_updates(requests);
            if (restored_)
            {
                restored_->collect_builtin_updates(requests);
            }
        }
        void on_build_ui() override
        {
            panel_.draw();
        }
        StaticMeshComponent* component()
        {
            auto* actor = dynamic_cast<StaticMeshActor*>(world().find_actor_by_id(actor_id_));
            return actor ? &actor->static_mesh_component() : nullptr;
        }
        bool write_source(const std::string& text)
        {
            const auto wrote = platform_.write_text_utf8(source_, text, FileWriteMode::Truncate);
            if (!wrote.succeeded())
            {
                stop(wrote.message);
                return false;
            }
            return true;
        }
        bool apply_candidate()
        {
            std::string error;
            if (!materials_.prepare_shader(shaders_.candidate(), error) ||
                !panel_.prepare_shader(shaders_.candidate(), shaders_.candidate_properties(), error))
            {
                stop(error);
                return false;
            }
            if (!materials_.publish_shader(error, true))
            {
                stop(error);
                return false;
            }
            if (!shaders_.publish())
            {
                materials_.discard_shader();
                stop(shaders_.error());
                return false;
            }
            materials_.complete_shader();
            panel_.publish_shader();
            return true;
        }
        void on_tick(double delta) override
        {
            elapsed_ += delta;
            ++frames_;
            if (elapsed_ > 120.0)
            {
                stop("Shader integration timed out: " + shaders_.status());
                return;
            }
            shaders_.tick();
            if (restored_)
            {
                restored_->tick();
            }
            if (phase_ == -1)
            {
                if (shaders_.candidate_ready() && !apply_candidate())
                {
                    return;
                }
                if (shaders_.busy())
                {
                    return;
                }
                if (shaders_.task_status().phase != ShaderTaskPhase::Completed ||
                    !shaders_.program("Toy3d/Surface/Unlit") || !shaders_.program("Toy3d/Surface/Phong") ||
                    !shaders_.error().empty())
                {
                    stop("Builtin startup validation failed: " + shaders_.error());
                    return;
                }
                // Unlit must be assignable through the same Library used by mesh slots.
                MaterialAssetData black;
                black.shader_name = "Toy3d/Surface/Unlit";
                AssetId black_id;
                if (!AssetId::try_generate(black_id))
                {
                    stop("AssetId failed.");
                    return;
                }
                const auto bytes = encode_material_asset_pair(workspace_.types(), black_id, black);
                const auto path = VirtualPath::parse("/Project/unlit.asset");
                if (!bytes.succeeded() ||
                    !workspace_.asset_pairs()
                         .publish(path.value(), bytes.value(), FilePublishMode::CreateNew)
                         .succeeded() ||
                    !workspace_.refresh())
                {
                    stop("Unlit test asset publication failed.");
                    return;
                }
                AssetRef reference;
                reference.asset_id = black_id;
                reference.expected_type = "toy3d.MaterialAssetData";
                const auto loaded = library_->load(reference);
                if (!loaded.succeeded() || loaded.value()->desc().shader_name != black.shader_name)
                {
                    stop("Unlit was not available for assignment.");
                    return;
                }
                MaterialAssetData data;
                data.shader_name = "Project/Surface/Painted";
                data.overrides = {{"stripe_scale", 8.0f}};
                if (!AssetId::try_generate(material_id_))
                {
                    stop("AssetId failed.");
                    return;
                }
                const auto painted = encode_material_asset_pair(workspace_.types(), material_id_, data);
                const auto painted_path = VirtualPath::parse("/Project/painted.asset");
                if (!painted.succeeded() ||
                    !workspace_.asset_pairs()
                         .publish(painted_path.value(), painted.value(), FilePublishMode::CreateNew)
                         .succeeded() ||
                    !workspace_.refresh())
                {
                    stop("Could not publish isolated test material.");
                    return;
                }
                asset_signature_ = sha256(painted.value().asset);
                AssetRef painted_ref;
                painted_ref.asset_id = material_id_;
                painted_ref.expected_type = "toy3d.MaterialAssetData";
                std::string error;
                if (history_.assign_material(world(), actor_id_, component()->component_id(), "Material_0", painted_ref,
                                             error) ||
                    !materials_.offer_compile_assignment(world(), actor_id_,
                                                         {component()->component_id(), "Material_0", painted_ref}) ||
                    !materials_.compile_assignment(error))
                {
                    stop("Unavailable registered Shader did not offer Compile and Assign: " + error);
                    return;
                }
                phase_ = 0;
                return;
            }
            if (restored_ && restored_->candidate_ready() &&
                restored_->candidate()->data().shader_name != "Project/Surface/Painted")
            {
                if (!restored_->publish())
                {
                    stop(restored_->error());
                    return;
                }
            }
            if (phase_ < 2 && !shaders_.error().empty())
            {
                stop(shaders_.error());
                return;
            }
            if (phase_ == 0)
            {
                if (shaders_.candidate_ready() && !apply_candidate())
                {
                    return;
                }
                if (shaders_.busy())
                {
                    return;
                }
                std::string error;
                materials_.tick_compile_assignment(world(), history_, error);
                if (!error.empty() ||
                    !(materials_.reference(world(), actor_id_, component()->component_id(), "Material_0").asset_id ==
                      material_id_) ||
                    !history_.undo(world()) || !history_.redo(world()) ||
                    !(materials_.reference(world(), actor_id_, component()->component_id(), "Material_0").asset_id ==
                      material_id_))
                {
                    stop("Compile and Assign did not preserve the normal Undo/Redo transaction: " + error);
                    return;
                }
                panel_.request_open(material_id_);
                phase_ = 1;
                return;
            }
            if (phase_ == 1 && panel_.edit_session().active() && !second_compile_)
            {
                auto& session = panel_.edit_session();
                if (!session.begin_gesture().succeeded() ||
                    !session.set_parameter({"base_color", Vector4(1, 0, 0, 1)}).succeeded() ||
                    !session.finish_gesture().succeeded())
                {
                    stop("Could not prepare a dirty material parameter gesture.");
                    return;
                }
                undo_count_ = session.undo_count();
                revised_ = text_;
                std::size_t offset = 0;
                while ((offset = revised_.find("stripe_scale", offset)) != std::string::npos)
                {
                    revised_.replace(offset, 12u, "stripe_frequency");
                    offset += 16u;
                }
                if (!write_source(revised_) ||
                    !shaders_.recompile("Project/Surface/Painted", material_id_, panel_.session_revision()))
                {
                    stop(shaders_.error());
                    return;
                }
                before_ = shaders_.program("Project/Surface/Painted");
                second_compile_ = true;
                return;
            }
            if (phase_ == 1 && second_compile_ && shaders_.candidate_ready())
            {
                if (shaders_.candidate() == before_)
                {
                    stop("Same logical key reused the old Program.");
                    return;
                }
                if (!apply_candidate())
                {
                    return;
                }
                const auto& session = panel_.edit_session();
                if (!session.dirty() || session.undo_count() != undo_count_ ||
                    component()->material_for_slot(0)->desc().shader_program !=
                        shaders_.program("Project/Surface/Painted"))
                {
                    stop("Code publication lost draft/history or failed to refresh scene material.");
                    return;
                }
                bool orphan = false;
                for (const auto& value : session.overrides())
                {
                    if (value.name == "stripe_scale" && !material_override_matches_schema(value, session.schema()))
                    {
                        orphan = true;
                    }
                }
                if (!orphan)
                {
                    stop("Removed parameter was not retained as an orphan.");
                    return;
                }
                const auto path = VirtualPath::parse("/Project/painted.asset");
                const auto bytes = workspace_.files().read_binary(path.value());
                if (!bytes.succeeded() || sha256(bytes.value()) != asset_signature_)
                {
                    stop("Recompile saved or modified the material asset.");
                    return;
                }
                before_ = shaders_.program("Project/Surface/Painted");
                mesh_gate_ = std::make_shared<std::atomic<int>>(0);
                enqueue_render_command("VerifyCompiledMeshDrawable",
                                       [mesh = component()->static_mesh(), gate = mesh_gate_]() noexcept
                                       {
                                           gate->store(mesh && mesh->render_data()->is_drawable() ? 1 : -1,
                                                       std::memory_order_release);
                                       });
                if (!write_source("invalid Shader source"))
                {
                    return;
                }
                if (shaders_.recompile("Project/Surface/Painted") || shaders_.busy() || shaders_.error().empty() ||
                    shaders_.task_status().failed != 1u)
                {
                    stop("Discovery must synchronously diagnose malformed saved source and finish its failed task.");
                    return;
                }
                phase_ = 2;
                return;
            }
            if (phase_ == 2 && !shaders_.busy() && !shaders_.error().empty())
            {
                const int drawable = mesh_gate_->load(std::memory_order_acquire);
                if (!drawable)
                {
                    return;
                }
                if (drawable < 0)
                {
                    stop("Material replacement released the active mesh geometry.");
                    return;
                }
                if (!shaders_.has_error_location() || !shaders_.open_error() ||
                    processes_.parameters[2].find(":1:") == std::string::npos)
                {
                    stop("Compiler source error location was not forwarded to VS Code.");
                    return;
                }
                if (shaders_.program("Project/Surface/Painted") != before_ ||
                    component()->material_for_slot(0)->desc().shader_program != before_)
                {
                    stop("Compiler failure replaced the old effect.");
                    return;
                }
                std::string unsupported = revised_;
                const auto pass = unsupported.find("Pass \"Forward\"");
                if (pass == std::string::npos)
                {
                    stop("Policy fixture has no Forward Pass.");
                    return;
                }
                unsupported.replace(pass, std::string("Pass \"Forward\"").size(), "Pass \"Preview\"");
                if (!write_source(unsupported))
                {
                    return;
                }
                if (shaders_.recompile("Project/Surface/Painted") || shaders_.busy() ||
                    shaders_.error().find("one Forward Material pass") == std::string::npos ||
                    shaders_.program("Project/Surface/Painted") != before_ ||
                    component()->material_for_slot(0)->desc().shader_program != before_)
                {
                    stop("Editor Pass policy must reject before compiling and preserve the published effect.");
                    return;
                }
                if (!write_source(revised_))
                {
                    return;
                }
                unsupported = revised_;
                unsupported.replace(unsupported.find("Project/Surface/Painted"),
                                    std::string("Project/Surface/Painted").size(), "Shared/Effects/Painted");
                const auto policy_source = platform_.join_relative(paths_.project_shader, "unsupported.shader");
                if (!policy_source.succeeded() ||
                    !platform_.write_text_utf8(policy_source.value(), unsupported, FileWriteMode::CreateNew)
                         .succeeded())
                {
                    stop("Cannot write the namespace policy fixture.");
                    return;
                }
                if (shaders_.recompile("Shared/Effects/Painted") || shaders_.busy() ||
                    shaders_.error().find("Project/Surface/") == std::string::npos ||
                    shaders_.program("Project/Surface/Painted") != before_)
                {
                    stop("Editor name policy must reject discovered declarations outside the project namespace.");
                    return;
                }
                if (!platform_.remove_file(policy_source.value()).succeeded())
                {
                    stop("Cannot remove the namespace policy fixture.");
                    return;
                }
                restored_ = std::make_unique<ShaderWorkflow>(processes_, threads_);
                std::string error;
                if (!restored_->initialize(paths_, factory_.default_material()->material(), error))
                {
                    stop(error);
                    return;
                }
                phase_ = 3;
                return;
            }
            if (phase_ == 3 && restored_->candidate_ready())
            {
                if (!restored_->publish() || !restored_->program("Project/Surface/Painted"))
                {
                    stop(restored_->error());
                    return;
                }
                std::string color = revised_;
                const auto input = color.find("float4 normal : NORMAL0;");
                if (input == std::string::npos)
                {
                    stop("Source test input changed.");
                    return;
                }
                color.insert(input, "float4 color : COLOR0; ");
                const auto expression = color.find("output.world_position = world.xyz;");
                color.replace(expression, 34u, "output.world_position = world.xyz + input.color.xyz * 0.01;");
                if (!write_source(color) || !shaders_.recompile("Project/Surface/Painted"))
                {
                    stop(shaders_.error());
                    return;
                }
                phase_ = 4;
                return;
            }
            if (phase_ == 4 && !shaders_.busy() && !shaders_.error().empty())
            {
                if (shaders_.error().find("VertexFactory") == std::string::npos)
                {
                    stop("GPU/VF candidate was not rejected for incompatible vertex inputs: " + shaders_.error());
                    return;
                }
                if (shaders_.program("Project/Surface/Painted") != before_ ||
                    component()->material_for_slot(0)->desc().shader_program != before_)
                {
                    stop("GPU/VF candidate rejection did not retain the previous program and scene effect.");
                    return;
                }
                if (!write_source(revised_))
                {
                    return;
                }
                if (!shaders_.recompile("Project/Surface/Painted"))
                {
                    stop(shaders_.error());
                    return;
                }
                phase_ = 5;
                return;
            }
            if (phase_ == 5 && shaders_.candidate_ready())
            {
                std::string error;
                if (!materials_.prepare_shader(shaders_.candidate(), error) ||
                    !panel_.prepare_shader(shaders_.candidate(), shaders_.candidate_properties(), error) ||
                    !materials_.publish_shader(error, true))
                {
                    stop(error);
                    return;
                }
                // Editing while RT validates must invalidate the ready candidate
                // and roll back provisional scene changes before retiring it.
                if (!write_source(revised_ + "\n// edited during candidate publication\n"))
                {
                    return;
                }
                if (shaders_.publish())
                {
                    stop("Changed source was published after GPU validation.");
                    return;
                }
                materials_.discard_shader();
                panel_.discard_shader();
                const auto failure = shaders_.error();
                shaders_.reject(failure);
                if (shaders_.program("Project/Surface/Painted") != before_ ||
                    component()->material_for_slot(0)->desc().shader_program != before_)
                {
                    stop("Rejected source publication did not roll back scene slots.");
                    return;
                }
                if (!write_source(revised_))
                {
                    return;
                }
                if (restored_ && restored_->busy())
                {
                    return;
                }
                if (!write_source("invalid Shader source") || !shaders_.recompile_all())
                {
                    stop(shaders_.error());
                    return;
                }
                phase_ = 6;
                return;
            }
            if (phase_ == 6)
            {
                if (shaders_.candidate_ready() && !apply_candidate())
                {
                    return;
                }
                if (shaders_.busy())
                {
                    return;
                }
                if (shaders_.task_status().phase != ShaderTaskPhase::Completed ||
                    shaders_.task_status().applied != 6u || shaders_.task_status().failed != 1u ||
                    shaders_.task_status().diagnostics.empty() ||
                    shaders_.task_status().diagnostics.back().message.empty() ||
                    shaders_.task_status().diagnostics.back().message.front() == '\n' ||
                    shaders_.status().find("6 applied, 1 failed") == std::string::npos ||
                    shaders_.program("Project/Surface/Painted") != before_ ||
                    factory_.default_material()->desc().shader_program != shaders_.program("Toy3d/Surface/Phong"))
                {
                    stop("Batch failure did not preserve the old project effect or refresh the shared default: " +
                         shaders_.status());
                    return;
                }
                if (!write_source(revised_) || !shaders_.recompile_all())
                {
                    stop(shaders_.error());
                    return;
                }
                shaders_.cancel();
                phase_ = 7;
                return;
            }
            if (phase_ == 7)
            {
                if (shaders_.busy())
                {
                    return;
                }
                if (shaders_.task_status().phase != ShaderTaskPhase::Completed ||
                    shaders_.task_status().cancelled != 7u || !shaders_.task_status().diagnostics.empty() ||
                    shaders_.status().find("0 applied, 0 failed, 7 cancelled") == std::string::npos)
                {
                    stop("Batch cancellation lost its summary: " + shaders_.status());
                    return;
                }
                if (!shaders_.recompile_all())
                {
                    stop(shaders_.error());
                    return;
                }
                phase_ = 8;
                return;
            }
            if (phase_ == 8)
            {
                if (shaders_.candidate_ready() && !apply_candidate())
                {
                    return;
                }
                if (shaders_.busy())
                {
                    return;
                }
                if (shaders_.status().find("7 applied, 0 failed") == std::string::npos)
                {
                    stop("Complete builtin/project batch failed: " + shaders_.status());
                    return;
                }
                const auto* imgui_source = shaders_.find("Toy3d/UI/ImGui");
                if (!imgui_source)
                {
                    stop("ImGui source registration missing.");
                    return;
                }
                imgui_source_ = PhysicalPath(paths_.engine_shader.utf8() + "/ui/imgui.shader");
                const auto original = platform_.read_text_utf8(imgui_source_);
                const auto record = platform_.read_text_utf8(PhysicalPath(paths_.saved.utf8() + "/globals.txt"));
                if (!original.succeeded() || !record.succeeded())
                {
                    stop("Cannot read isolated Global source/publication record.");
                    return;
                }
                imgui_text_ = original.value();
                global_record_ = record.value();
                for (const auto& source : shaders_.sources())
                {
                    if (source.usage == BuiltinShaderUsage::Global)
                    {
                        global_before_.push_back(source.program);
                    }
                }
                std::string incompatible = imgui_text_;
                const auto depth = incompatible.find("DepthTest Off");
                if (depth == std::string::npos)
                {
                    stop("ImGui source depth state changed.");
                    return;
                }
                incompatible.replace(depth, std::string("DepthTest Off").size(), "DepthTest GreaterEqual");
                if (!platform_.write_text_utf8(imgui_source_, incompatible, FileWriteMode::Truncate).succeeded() ||
                    !shaders_.recompile_all())
                {
                    stop("Cannot start incompatible Global pipeline validation.");
                    return;
                }
                phase_ = 10;
                return;
            }
            if (phase_ == 10 || phase_ == 11)
            {
                if (shaders_.candidate_ready() && !apply_candidate())
                {
                    return;
                }
                if (shaders_.busy())
                {
                    return;
                }
                std::size_t index = 0u;
                for (const auto& source : shaders_.sources())
                {
                    if (source.usage == BuiltinShaderUsage::Global)
                    {
                        if (source.program != global_before_[index++])
                        {
                            stop("Failed Global group replaced a published Program.");
                            return;
                        }
                    }
                }
                const auto record = platform_.read_text_utf8(PhysicalPath(paths_.saved.utf8() + "/globals.txt"));
                if (shaders_.status().find("4 applied, 3 failed") == std::string::npos || !record.succeeded() ||
                    record.value() != global_record_)
                {
                    stop("Failed Global group changed Saved publication or stopped later jobs: " + shaders_.status());
                    return;
                }
                const std::string next = phase_ == 10 ? "invalid Global Shader source" : imgui_text_;
                if (!platform_.write_text_utf8(imgui_source_, next, FileWriteMode::Truncate).succeeded() ||
                    !shaders_.recompile_all())
                {
                    stop("Cannot start Global failure/recovery batch.");
                    return;
                }
                phase_ = phase_ == 10 ? 11 : 12;
                return;
            }
            if (phase_ == 12)
            {
                if (shaders_.candidate_ready() && !apply_candidate())
                {
                    return;
                }
                if (shaders_.busy())
                {
                    return;
                }
                if (shaders_.status().find("7 applied, 0 failed") == std::string::npos)
                {
                    stop("Global group did not recover: " + shaders_.status());
                    return;
                }
                restored_->shutdown();
                restored_ = std::make_unique<ShaderWorkflow>(processes_, threads_);
                std::string error;
                if (!restored_->initialize(paths_, factory_.default_material()->material(), error))
                {
                    stop(error);
                    return;
                }
                phase_ = 9;
                return;
            }
            if (phase_ == 9)
            {
                if (restored_->candidate_ready() && !restored_->publish())
                {
                    stop(restored_->error());
                    return;
                }
                if (restored_->busy())
                {
                    return;
                }
                if (!restored_->error().empty() || !restored_->program("Toy3d/UI/ImGui") ||
                    !restored_->program("Toy3d/Surface/Unlit"))
                {
                    stop("Published Global/material versions did not restore: " + restored_->error());
                    return;
                }
                const auto published = shaders_.program("Toy3d/Surface/Phong");
                if (shaders_.create_source("Toy3d/Surface/Overwrite", "overwrite.shader", "Toy3d/Surface/Unlit") ||
                    shaders_.create_source("Project/Surface/Escape", "../escape.shader", "Toy3d/Surface/Unlit") ||
                    shaders_.create_source("Project/Surface/Overwrite", "painted.shader", "Toy3d/Surface/Unlit") ||
                    shaders_.create_source("Project/Surface/Global", "global.shader", "Toy3d/UI/ImGui"))
                {
                    stop("Shader creation accepted an invalid name/path/template or overwrote a source.");
                    return;
                }
                if (!shaders_.create_source("Project/Surface/CreatedUnlit", "created/unlit.shader",
                                            "Toy3d/Surface/Unlit") ||
                    !shaders_.create_source("Project/Surface/CreatedPhong", "created/phong.shader",
                                            "Toy3d/Surface/Phong") ||
                    shaders_.program("Toy3d/Surface/Phong") != published ||
                    !shaders_.find("Project/Surface/CreatedUnlit") ||
                    !shaders_.recompile("Project/Surface/CreatedUnlit"))
                {
                    stop("Shader creation lost registration/published programs: " + shaders_.error());
                    return;
                }
                phase_ = 13;
                return;
            }
            if (phase_ == 13 || phase_ == 14)
            {
                if (shaders_.candidate_ready() && !apply_candidate())
                {
                    return;
                }
                if (shaders_.busy())
                {
                    return;
                }
                const std::string name = phase_ == 13 ? "Project/Surface/CreatedUnlit" : "Project/Surface/CreatedPhong";
                if (!shaders_.program(name) || !shaders_.error().empty())
                {
                    stop("New template Shader did not compile/validate: " + shaders_.error());
                    return;
                }
                if (phase_ == 13)
                {
                    if (!shaders_.recompile("Project/Surface/CreatedPhong"))
                    {
                        stop(shaders_.error());
                        return;
                    }
                    phase_ = 14;
                    return;
                }
                restored_->shutdown();
                restored_ = std::make_unique<ShaderWorkflow>(processes_, threads_);
                std::string error;
                if (!restored_->initialize(paths_, factory_.default_material()->material(), error) ||
                    !restored_->find("Project/Surface/CreatedUnlit") ||
                    !restored_->find("Project/Surface/CreatedPhong"))
                {
                    stop("Created Shader registrations did not survive restart: " + error);
                    return;
                }
                state_.complete = true;
                window().close();
            }
        }
        void stop(const std::string& error)
        {
            state_.error = error;
            std::cerr << error << '\n';
            window().close();
        }
        void on_shutdown() override
        {
            shaders_.shutdown();
            if (restored_)
            {
                restored_->shutdown();
            }
            panel_.shutdown();
            for (const auto id : world().actor_ids())
            {
                auto* actor = world().find_actor_by_id(id);
                if (actor && !world().destroy_actor(*actor))
                {
                    state_.error = "Actor teardown failed.";
                }
            }
            if (!flush_rendering_commands().succeeded())
            {
                state_.error = "Scene drain failed.";
            }
            history_.clear();
            materials_.shutdown();
            library_->shutdown();
            library_.reset();
            factory_.release();
        }
        EditorWorkspace& workspace_;
        ShaderWorkflowPaths paths_;
        PhysicalPath source_;
        std::string text_;
        std::string revised_;
        TestState& state_;
        RecordingProcesses processes_;
        ThreadManager threads_;
        ShaderWorkflow shaders_{processes_, threads_};
        std::unique_ptr<ShaderWorkflow> restored_;
        NativePlatformFile platform_;
        ActorFactory factory_;
        std::unique_ptr<MaterialLibrary> library_;
        MaterialAssignments materials_;
        MaterialEditorPanel panel_;
        EditorCommandHistory history_{factory_, materials_};
        PhysicalPath imgui_source_;
        std::string imgui_text_;
        std::string global_record_;
        std::vector<ShaderMapProgramRef> global_before_;
        std::uint32_t actor_id_ = 0;
        AssetId material_id_;
        Sha256Hash asset_signature_{};
        ShaderMapProgramRef before_;
        std::shared_ptr<std::atomic<int>> mesh_gate_;
        std::size_t undo_count_ = 0;
        int phase_ = -1;
        bool second_compile_ = false;
        std::uint64_t frames_ = 0;
        double elapsed_ = 0;
    };
} // namespace

int main()
{
    using namespace toy3d;
    NativePlatformFile platform;
    AssetId id;
    if (!AssetId::try_generate(id))
    {
        return 1;
    }
    const auto root = platform.join_relative(PhysicalPath(TOY3D_SHADER_TEST_ROOT), id.hex());
    if (!root.succeeded())
    {
        return 1;
    }
    ShaderWorkflowPaths paths;
    paths.project_shader = PhysicalPath(root.value().utf8() + "/shader");
    paths.saved = PhysicalPath(root.value().utf8() + "/saved");
    paths.engine_shader = PhysicalPath(root.value().utf8() + "/builtin");
    // Copies preserve virtual identities/hashes while failed reloads remain
    // isolated from developer source assets and parallel test processes.
    for (const auto& builtin : builtin_shader_sources)
    {
        const std::string relative = builtin.source;
        const PhysicalPath destination(paths.engine_shader.utf8() + "/" + relative);
        const PhysicalPath parent(destination.utf8().substr(0u, destination.utf8().find_last_of('/')));
        const auto source_text =
            platform.read_text_utf8(PhysicalPath(std::string(TOY3D_EDITOR_ENGINE_SHADER_ROOT) + "/" + relative));
        if (!platform.create_directories(parent).succeeded() || !source_text.succeeded() ||
            !platform.write_text_utf8(destination, source_text.value(), FileWriteMode::CreateNew).succeeded())
        {
            return 1;
        }
    }
    paths.engine_include = PhysicalPath(TOY3D_EDITOR_ENGINE_INCLUDE_ROOT);
    paths.builtin_root = PhysicalPath(TOY3D_BUILTIN_SHADER_ROOT);
    paths.compiler = PhysicalPath(TOY3D_EDITOR_SHADER_COMPILER);
    paths.toolchain = PhysicalPath(TOY3D_EDITOR_SHADER_TOOLCHAIN);
    paths.code_executable = PhysicalPath(root.value().utf8() + "/VS Code/Code.exe");
    if (!platform.create_directories(paths.project_shader).succeeded())
    {
        return 1;
    }
    const PhysicalPath include_root(paths.project_shader.utf8() + "/include");
    const auto include_text = platform.read_text_utf8(PhysicalPath(TOY3D_SHADER_TEST_INCLUDE));
    if (!platform.create_directories(include_root).succeeded() || !include_text.succeeded() ||
        !platform
             .write_text_utf8(PhysicalPath(include_root.utf8() + "/project_common.hlsli"), include_text.value(),
                              FileWriteMode::CreateNew)
             .succeeded())
    {
        return 1;
    }
    const auto text = platform.read_text_utf8(PhysicalPath(TOY3D_SHADER_TEST_SOURCE));
    const PhysicalPath source(paths.project_shader.utf8() + "/painted.shader");
    if (!text.succeeded() || !platform.write_text_utf8(source, text.value(), FileWriteMode::CreateNew).succeeded())
    {
        return 1;
    }
    EditorWorkspace workspace;
    EditorWorkspacePaths workspace_paths;
    workspace_paths.project_assets = PhysicalPath(root.value().utf8() + "/asset");
    workspace_paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    workspace_paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
    workspace_paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
    if (!platform.create_directories(workspace_paths.project_assets).succeeded() ||
        !workspace.initialize(workspace_paths))
    {
        return 1;
    }
    // Stale manual publication must fall back to the current deployed Phong.
    const auto stale_directory = platform.join_relative(paths.saved, sha256_to_hex(sha256("Toy3d/Surface/Phong")));
    if (!stale_directory.succeeded() || !platform.create_directories(stale_directory.value()).succeeded() ||
        !platform
             .write_text_utf8(PhysicalPath(stale_directory.value().utf8() + "/current.txt"),
                              "requests/11111111111111111111111111111111\n" + std::string(64u, '0') + "\n",
                              FileWriteMode::CreateNew)
             .succeeded())
    {
        return 1;
    }
    CommandLineParser::get_instance().parser_args(
        {"ShaderTests", "--Window.Width=720", "--Window.Height=480", "--Window.Title=Material Shader Tests"});
    TestState state;
    {
        Engine engine;
        engine.set_shader_load_config({ShaderLoadMode::ShaderMapEntry, PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT)});
        engine.set_application(std::make_unique<ShaderTestApplication>(workspace, paths, source, text.value(), state));
#if WITH_WIN
        engine.init(static_cast<void*>(GetModuleHandleW(nullptr)));
#else
        engine.init(nullptr);
#endif
        engine.main_loop();
        engine.exit();
    }
    std::cout << "Shader test artifacts: " << root.value().utf8() << '\n';
    if (!state.complete || !state.error.empty())
    {
        std::cerr << "Shader integration failed: " << state.error << '\n';
        return 1;
    }
    std::cout << "Real compile, Vulkan pipeline validation, schema/orphan/draft, slot publication, failed code "
                 "fallback and saved revision restore passed.\n";
    return 0;
}
