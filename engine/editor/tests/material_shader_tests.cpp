#include "application/application.h"

#include <iostream>
#include <memory>
#include <string>

#include "imgui.h"
#if WITH_WIN64
#include <Windows.h>
#endif
#include "config/command_line_parser.h"
#include "engine.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/world/world.h"
#include "material/material_assignments.h"
#include "material/material_shader_workflow.h"
#include "panels/material_editor_panel.h"
#include "placement/actor_factory.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/render_command.h"
#include "renderscene/geometry/static_mesh_render_data.h"
#include "workspace/editor_workspace.h"

namespace
{
    using namespace toy3d;
    struct TestState { bool complete = false; std::string error; };

    // --------------------------------------------------------------------------
    // RecordingProcesses: real compiler execution with isolated VS Code launch capture
    // --------------------------------------------------------------------------
    class RecordingProcesses final : public ProcessService
    {
      public:
        ProcessResult run(const PhysicalPath& executable, const std::vector<std::string>& arguments,
                          const ProcessRunOptions& options) const override { return native_.run(executable, arguments, options); }
        ProcessResult launch_detached(const PhysicalPath& executable, const std::vector<std::string>& arguments) const override
        { opened = executable; parameters = arguments; return {true, 0, {}}; }
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
        ShaderTestApplication(EditorWorkspace& workspace, MaterialShaderPaths paths, PhysicalPath source,
                              std::string text, TestState& state)
            : workspace_(workspace), paths_(std::move(paths)), source_(std::move(source)), text_(std::move(text)), state_(state) {}
      private:
        bool on_initialize() override
        {
            ImGui::GetIO().IniFilename = nullptr;
            if (!factory_.initialize()) return false;
            materials_.initialize(workspace_, factory_.default_material()->material());
            panel_.initialize(workspace_, factory_.default_material()->material(), PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
            std::string error;
            if (!shaders_.initialize(paths_, factory_.default_material()->material(), error)) { state_.error = error; return false; }
            materials_.set_program_resolver([this](const std::string& name) { return shaders_.program(name); });
            panel_.set_shader_workflow(shaders_);
            if (!shaders_.open_source("Project/Surface/Painted", 12u, 3u) || processes_.parameters.size() != 3u ||
                processes_.parameters[2].find(":12:3") == std::string::npos || processes_.opened != paths_.code_executable)
            { state_.error = "VS Code did not receive the registered author source/line arguments."; return false; }
            PlacementRequest placement; placement.item = PlacementItemId::Cube;
            auto* actor = dynamic_cast<StaticMeshActor*>(factory_.create(world(), placement));
            if (!actor) return false;
            actor_id_ = actor->actor_id();
            return shaders_.recompile("Project/Surface/Painted");
        }
        bool starts_world_play() const override { return false; }
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override
        {
            views.emplace_back(Vector3(0, 0, -3), Quaternion::identity(), Vector3(0, 0, 1),
                IntRect{0, 0, extent.width, extent.height}, extent, CameraProjectionMode::Perspective,
                Radians(0.785398163f), 0.1f, 100.0f);
        }
        void on_collect_material_validation(std::vector<MaterialProgramValidationRef>& requests) override
        { shaders_.collect_validation(requests); if (restored_) restored_->collect_validation(requests); }
        void on_build_ui() override { panel_.draw(); }
        StaticMeshComponent* component()
        {
            auto* actor = dynamic_cast<StaticMeshActor*>(world().find_actor_by_id(actor_id_));
            return actor ? &actor->static_mesh_component() : nullptr;
        }
        bool write_source(const std::string& text)
        {
            const auto wrote = platform_.write_text_utf8(source_, text, FileWriteMode::Truncate);
            if (!wrote.succeeded()) { stop(wrote.message); return false; } return true;
        }
        bool apply_candidate()
        {
            std::string error;
            if (!materials_.prepare_shader(shaders_.candidate(), error) ||
                !panel_.prepare_shader(shaders_.candidate(), shaders_.candidate_properties(), error))
            { stop(error); return false; }
            if (!materials_.publish_shader(error, true)) { stop(error); return false; }
            if (!shaders_.publish()) { materials_.discard_shader(); stop(shaders_.error()); return false; }
            materials_.complete_shader();
            panel_.publish_shader(); return true;
        }
        void on_tick(double delta) override
        {
            elapsed_ += delta; ++frames_;
            if (elapsed_ > 45.0) { stop("Shader integration timed out: " + shaders_.status()); return; }
            shaders_.tick();
            if (restored_) restored_->tick();
            if (phase_ < 2 && !shaders_.error().empty()) { stop(shaders_.error()); return; }
            if (phase_ == 0 && shaders_.candidate_ready())
            {
                if (!apply_candidate()) return;
                MaterialAssetData data; data.shader_name = "Project/Surface/Painted";
                data.overrides = {{"stripe_scale", 8.0f}};
                if (!AssetId::try_generate(material_id_)) { stop("AssetId failed."); return; }
                const auto bytes = encode_material_asset(workspace_.types(), material_id_, data);
                const auto path = VirtualPath::parse("/Project/painted.asset");
                if (!bytes.succeeded() || !workspace_.files().write_binary_atomic(path.value(), bytes.value(), FilePublishMode::CreateNew).succeeded() || !workspace_.refresh())
                { stop("Could not publish isolated test material."); return; }
                asset_signature_ = sha256(bytes.value());
                AssetRef reference; reference.asset_id = material_id_; reference.expected_type = "toy3d.MaterialAssetData";
                std::string error;
                if (!materials_.assign(world(), actor_id_, {component()->component_id(), "Material_0", reference}, error)) { stop(error); return; }
                panel_.request_open(material_id_); phase_ = 1; return;
            }
            if (phase_ == 1 && workspace_.material_edit().active() && !second_compile_)
            {
                auto& session = workspace_.material_edit();
                if (!session.begin_gesture().succeeded() || !session.set_parameter({"base_color", Vector4(1, 0, 0, 1)}).succeeded() || !session.finish_gesture().succeeded())
                { stop("Could not prepare a dirty material parameter gesture."); return; }
                undo_count_ = session.undo_count();
                revised_ = text_;
                std::size_t offset = 0;
                while ((offset = revised_.find("stripe_scale", offset)) != std::string::npos)
                { revised_.replace(offset, 12u, "stripe_frequency"); offset += 16u; }
                if (!write_source(revised_) || !shaders_.recompile("Project/Surface/Painted", material_id_, panel_.session_revision()))
                { stop(shaders_.error()); return; }
                before_ = shaders_.program("Project/Surface/Painted"); second_compile_ = true; return;
            }
            if (phase_ == 1 && second_compile_ && shaders_.candidate_ready())
            {
                if (shaders_.candidate() == before_) { stop("Same logical key reused the old Program."); return; }
                if (!apply_candidate()) return;
                const auto& session = workspace_.material_edit();
                if (!session.dirty() || session.undo_count() != undo_count_ ||
                    component()->material_for_slot(0)->material()->desc().shader_program != shaders_.program("Project/Surface/Painted"))
                { stop("Code publication lost draft/history or failed to refresh scene material."); return; }
                bool orphan = false;
                for (const auto& value : session.overrides()) if (value.name == "stripe_scale" && !material_override_matches_schema(value, session.schema())) orphan = true;
                if (!orphan) { stop("Removed parameter was not retained as an orphan."); return; }
                const auto path = VirtualPath::parse("/Project/painted.asset");
                const auto bytes = workspace_.files().read_binary(path.value());
                if (!bytes.succeeded() || sha256(bytes.value()) != asset_signature_) { stop("Recompile saved or modified the material asset."); return; }
                before_ = shaders_.program("Project/Surface/Painted");
                mesh_gate_ = std::make_shared<std::atomic<int>>(0);
                enqueue_render_command("VerifyCompiledMeshDrawable", [mesh = component()->static_mesh(), gate = mesh_gate_]() noexcept
                { gate->store(mesh && mesh->render_data()->is_drawable() ? 1 : -1, std::memory_order_release); });
                if (!write_source("invalid Shader source") || !shaders_.recompile("Project/Surface/Painted")) { stop(shaders_.error()); return; }
                phase_ = 2; return;
            }
            if (phase_ == 2 && !shaders_.busy() && !shaders_.error().empty())
            {
                const int drawable = mesh_gate_->load(std::memory_order_acquire);
                if (!drawable) return;
                if (drawable < 0) { stop("Material replacement released the active mesh geometry."); return; }
                if (!shaders_.has_error_location() || !shaders_.open_error() || processes_.parameters[2].find(":1:") == std::string::npos)
                { stop("Compiler source error location was not forwarded to VS Code."); return; }
                if (shaders_.program("Project/Surface/Painted") != before_ || component()->material_for_slot(0)->material()->desc().shader_program != before_)
                { stop("Compiler failure replaced the old effect."); return; }
                if (!write_source(revised_)) return;
                restored_ = std::make_unique<MaterialShaderWorkflow>(processes_, threads_);
                std::string error;
                if (!restored_->initialize(paths_, factory_.default_material()->material(), error)) { stop(error); return; }
                phase_ = 3; return;
            }
            if (phase_ == 3 && restored_->candidate_ready())
            {
                if (!restored_->publish() || !restored_->program("Project/Surface/Painted")) { stop(restored_->error()); return; }
                std::string color = revised_;
                const auto input = color.find("float4 normal : NORMAL0;");
                if (input == std::string::npos) { stop("Source test input changed."); return; }
                color.insert(input, "float4 color : COLOR0; ");
                const auto expression = color.find("output.world_position = world.xyz;");
                color.replace(expression, 34u, "output.world_position = world.xyz + input.color.xyz * 0.01;");
                if (!write_source(color) || !shaders_.recompile("Project/Surface/Painted")) { stop(shaders_.error()); return; }
                phase_ = 4; return;
            }
            if (phase_ == 4 && !shaders_.busy() && !shaders_.error().empty())
            {
                if (shaders_.error().find("LocalVertexFactory") == std::string::npos || shaders_.program("Project/Surface/Painted") != before_)
                { stop("GPU/VF candidate rejection did not retain the previous effect: " + shaders_.error()); return; }
                if (!write_source(revised_)) return;
                if (!shaders_.recompile("Project/Surface/Painted")) { stop(shaders_.error()); return; }
                phase_ = 5; return;
            }
            if (phase_ == 5 && shaders_.candidate_ready())
            {
                std::string error;
                if (!materials_.prepare_shader(shaders_.candidate(), error) ||
                    !panel_.prepare_shader(shaders_.candidate(), shaders_.candidate_properties(), error) ||
                    !materials_.publish_shader(error, true)) { stop(error); return; }
                // Editing while RT validates must invalidate the ready candidate
                // and roll back provisional scene changes before retiring it.
                if (!write_source(revised_ + "\n// edited during candidate publication\n")) return;
                if (shaders_.publish()) { stop("Changed source was published after GPU validation."); return; }
                materials_.discard_shader(); panel_.discard_shader();
                const auto failure = shaders_.error(); shaders_.reject(failure);
                if (shaders_.program("Project/Surface/Painted") != before_ ||
                    component()->material_for_slot(0)->material()->desc().shader_program != before_)
                { stop("Rejected source publication did not roll back scene slots."); return; }
                if (!write_source(revised_)) return;
                state_.complete = true; window().close();
            }
        }
        void stop(const std::string& error) { state_.error = error; std::cerr << error << '\n'; window().close(); }
        void on_shutdown() override
        {
            shaders_.shutdown(); if (restored_) restored_->shutdown();
            panel_.shutdown();
            for (const auto id : world().actor_ids()) { auto* actor = world().find_actor_by_id(id); if (actor && !world().destroy_actor(*actor)) state_.error = "Actor teardown failed."; }
            if (!flush_rendering_commands().succeeded()) state_.error = "Scene drain failed.";
            materials_.shutdown(); factory_.release();
        }
        EditorWorkspace& workspace_;
        MaterialShaderPaths paths_;
        PhysicalPath source_;
        std::string text_;
        std::string revised_;
        TestState& state_;
        RecordingProcesses processes_;
        ThreadManager threads_;
        MaterialShaderWorkflow shaders_{processes_, threads_};
        std::unique_ptr<MaterialShaderWorkflow> restored_;
        NativePlatformFile platform_;
        ActorFactory factory_;
        MaterialAssignments materials_;
        MaterialEditorPanel panel_;
        std::uint32_t actor_id_ = 0;
        AssetId material_id_;
        Sha256Hash asset_signature_{};
        ShaderMapProgramRef before_;
        std::shared_ptr<std::atomic<int>> mesh_gate_;
        std::size_t undo_count_ = 0;
        int phase_ = 0;
        bool second_compile_ = false;
        std::uint64_t frames_ = 0;
        double elapsed_ = 0;
    };
}

int main()
{
    using namespace toy3d;
    NativePlatformFile platform;
    AssetId id; if (!AssetId::try_generate(id)) return 1;
    const auto root = platform.join_relative(PhysicalPath(TOY3D_SHADER_TEST_ROOT), id.hex());
    if (!root.succeeded()) return 1;
    MaterialShaderPaths paths;
    paths.project_shader = PhysicalPath(root.value().utf8() + "/shader");
    paths.project_config = PhysicalPath(root.value().utf8() + "/config");
    paths.saved = PhysicalPath(root.value().utf8() + "/saved");
    paths.engine_shader = PhysicalPath(TOY3D_EDITOR_ENGINE_SHADER_ROOT);
    paths.engine_include = PhysicalPath(TOY3D_EDITOR_ENGINE_INCLUDE_ROOT);
    paths.builtin_entries = PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT);
    paths.compiler = PhysicalPath(TOY3D_EDITOR_SHADER_COMPILER);
    paths.toolchain = PhysicalPath(TOY3D_EDITOR_SHADER_TOOLCHAIN);
    paths.code_executable = PhysicalPath(root.value().utf8() + "/VS Code/Code.exe");
    if (!platform.create_directories(paths.project_shader).succeeded() || !platform.create_directories(paths.project_config).succeeded()) return 1;
    const PhysicalPath include_root(paths.project_shader.utf8() + "/include");
    const auto include_text = platform.read_text_utf8(PhysicalPath(TOY3D_SHADER_TEST_INCLUDE));
    if (!platform.create_directories(include_root).succeeded() || !include_text.succeeded() ||
        !platform.write_text_utf8(PhysicalPath(include_root.utf8() + "/project_common.hlsli"), include_text.value(), FileWriteMode::CreateNew).succeeded()) return 1;
    const auto text = platform.read_text_utf8(PhysicalPath(TOY3D_SHADER_TEST_SOURCE));
    const PhysicalPath source(paths.project_shader.utf8() + "/painted.shader");
    if (!text.succeeded() || !platform.write_text_utf8(source, text.value(), FileWriteMode::CreateNew).succeeded() ||
        !platform.write_text_utf8(PhysicalPath(paths.project_config.utf8() + "/shader_sources.txt"),
            "Toy3dShaderSources 1\nProject/Surface/Painted\tpainted.shader\n", FileWriteMode::CreateNew).succeeded()) return 1;
    EditorWorkspace workspace;
    EditorWorkspacePaths workspace_paths;
    workspace_paths.project_assets = PhysicalPath(root.value().utf8() + "/asset");
    workspace_paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    workspace_paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
    workspace_paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
    if (!platform.create_directories(workspace_paths.project_assets).succeeded() || !workspace.initialize(workspace_paths)) return 1;
    CommandLineParser::get_instance().parser_args({"ShaderTests", "--Window.Width=720", "--Window.Height=480", "--Window.Title=Material Shader Tests"});
    TestState state;
    {
        Engine engine;
        engine.set_shader_load_config({ShaderLoadMode::ShaderMapEntry, PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT)});
        engine.set_application(std::make_unique<ShaderTestApplication>(workspace, paths, source, text.value(), state));
#if WITH_WIN64
        engine.init(static_cast<void*>(GetModuleHandleW(nullptr)));
#else
        engine.init(nullptr);
#endif
        engine.main_loop(); engine.exit();
    }
    std::cout << "Shader test artifacts: " << root.value().utf8() << '\n';
    if (!state.complete || !state.error.empty()) { std::cerr << "Shader integration failed: " << state.error << '\n'; return 1; }
    std::cout << "Real compile, Vulkan pipeline validation, schema/orphan/draft, slot publication, failed code fallback and saved revision restore passed.\n";
    return 0;
}
