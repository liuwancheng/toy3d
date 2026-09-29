#include "application/application.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"
#if WITH_WIN64
#include <windows.h>
#include <shlobj.h>
#include "platform/win/win32_window.h"
#endif

#include "config/command_line_parser.h"
#include "engine.h"
#include "file_system/native_platform_file.h"
#include "image_codec/png_codec.h"
#include "placement/actor_factory.h"
#include "placement/asset_placement.h"
#include "commands/editor_command_history.h"
#include "gamescene/world/world.h"
#include "gamescene/actor/actor.h"
#include "panels/content_browser_panel.h"
#include "panels/static_mesh_import_dialog.h"
#include "selection/editor_selection.h"
#include "rendercore/frame_synchronization.h"
#include "thumbnails/asset_thumbnail_pool.h"
#include "workspace/editor_workspace.h"
#if WITH_MODEL_IMPORT
#include "asset_tools/static_mesh_asset_tools.h"
#include "asset_import/static_mesh_import.h"
#endif

namespace
{
    using namespace toy3d;

    struct TestState
    {
        bool complete = false;
        std::string error;
        int captures = 0;
        int uploads = 0;
    };

    class ThumbnailTestApplication final : public Application
    {
      public:
        ThumbnailTestApplication(EditorWorkspace& workspace, AssetId first, AssetId second, TestState& state)
            : workspace_(workspace), pool_(workspace), history_(factory_), first_(first), second_(second), state_(state) {}

      private:
        bool on_initialize() override
        {
            ImGui::GetIO().IniFilename = nullptr;
#if WITH_WIN64
            if (!verify_native_drop()) return false;
#endif
            return factory_.initialize();
        }
        bool starts_world_play() const override { return false; }
        bool uses_preview_scene() const override { return true; }
        bool on_initialize_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks) override
        { return pool_.initialize(scene, factory_.default_material(), tasks); }
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override
        {
            views.emplace_back(Vector3(0, 0, -3), Quaternion::identity(), Vector3(0, 0, 1),
                IntRect{0, 0, extent.width, extent.height}, extent, CameraProjectionMode::Perspective,
                Radians(0.785398163f), 0.1f, 100.0f);
        }
        void on_collect_ui_render_work(UiRenderWork& work) override { pool_.collect_render_work(work); }
        std::vector<ImGuiTextureId> ui_texture_ids() const override { return pool_.texture_ids(); }
        void on_ui_texture_result(UiTextureResult result) override
        {
            const bool capture = !result.bgra_pixels.empty();
            if (result.succeeded()) { if (capture) ++state_.captures; else ++state_.uploads; }
            pool_.on_texture_result(std::move(result));
            if (phase_ == 3 && capture)
            {
                const auto path = VirtualPath::parse("/Project/first.asset");
                const auto bytes = workspace_.files().read_binary(path.value());
                if (!bytes.succeeded()) { stop(bytes.status().message); return; }
                const auto changed = replace_asset_segments(bytes.value(), {{"future_editor_data", 47, false, {9, 8, 7}}});
                if (!changed.succeeded()) { stop(changed.status().message); return; }
                const auto saved = workspace_.files().write_binary_atomic(path.value(), changed.value(), FilePublishMode::Replace);
                if (!saved.succeeded()) { stop(saved.message); return; }
                conflict_snapshot_ = sha256(changed.value());
            }
        }
        void on_tick(double delta) override
        {
            elapsed_ += delta;
            if (elapsed_ > 30.0) { stop("Thumbnail integration timed out."); return; }
            pool_.tick();
            if (!started_)
            {
                started_ = true;
                if (!verify_asset_placement()) return;
                pool_.generate(first_, true);
            }
            const auto a = request(first_);
            const auto b = phase_ > 0 ? request(second_) : AssetThumbnailView{};
            if (phase_ != 3 && !a.error.empty()) { stop(a.error); return; }
            if (phase_ > 0 && !b.error.empty()) { stop(b.error); return; }
            if (phase_ == 0 && a.texture_id.valid() && !a.busy)
            {
                if (!verify_saved("/Project/first.asset", {1, 2, 3})) return;
                pool_.generate(second_, true);
                phase_ = 1;
            }
            else if (phase_ == 1 && b.texture_id.valid() && !b.busy)
            {
                if (!verify_saved("/Project/second.asset", {1, 2, 3})) return;
                pool_.invalidate();
                phase_ = 2;
            }
            else if (phase_ == 2 && a.texture_id.valid() && b.texture_id.valid() && !a.busy && !b.busy)
            {
                if (state_.captures != 2 || state_.uploads != 2) { stop("Saved PNG reload did not use the image upload path."); return; }
                pool_.generate(first_, true);
                phase_ = 3;
            }
            else if (phase_ == 3 && !a.busy && !a.error.empty())
            {
                if (a.error.find("conflict") == std::string::npos) { stop("Expected an asset save conflict."); return; }
                const auto path = VirtualPath::parse("/Project/first.asset");
                const auto bytes = workspace_.files().read_binary(path.value());
                if (!bytes.succeeded() || sha256(bytes.value()) != conflict_snapshot_)
                { stop("A stale thumbnail overwrote the changed asset."); return; }
                pool_.generate(first_, true);
                phase_ = 4;
            }
            else if (phase_ == 4 && a.texture_id.valid() && !a.busy)
            {
                if (!verify_saved("/Project/first.asset", {9, 8, 7})) return;
                state_.complete = true;
                window().close();
            }
        }
        AssetThumbnailView request(const AssetId& id)
        {
            for (const auto& asset : workspace_.catalog().entries)
                if (asset.file.asset_id == id) return pool_.request(asset);
            stop("Test asset vanished from the catalog.");
            return {};
        }
        bool verify_saved(const std::string& name, const std::vector<std::uint8_t>& opaque)
        {
            const auto path = VirtualPath::parse(name);
            const auto index = inspect_asset(workspace_.files(), path.value());
            if (!index.succeeded()) { stop(index.status().message); return false; }
            bool found_thumbnail = false;
            bool found_opaque = false;
            for (const auto& segment : index.value().segments)
            {
                if (segment.name != "thumbnail" && segment.name != "future_editor_data") continue;
                const auto bytes = read_asset_segment(workspace_.files(), path.value(), index.value().asset_id, segment, thumbnail_max_bytes + 1024);
                if (!bytes.succeeded()) { stop(bytes.status().message); return false; }
                if (segment.name == "future_editor_data")
                    found_opaque = segment.kind == 47 && !segment.required && bytes.value() == opaque;
                else
                {
                    const auto thumbnail = decode_asset_thumbnail(bytes.value());
                    Rgba8Image image;
                    if (!thumbnail.succeeded() || !decode_png(thumbnail.value().png, image).succeeded())
                    { stop("Saved thumbnail is not a valid PNG."); return false; }
                    int brightest = 0;
                    int darkest = 255;
                    for (std::size_t i = 0; i < image.pixels.size(); i += 4)
                    {
                        brightest = std::max(brightest, static_cast<int>(image.pixels[i]));
                        darkest = std::min(darkest, static_cast<int>(image.pixels[i]));
                        if (image.pixels[i + 3] != 255) { stop("Thumbnail alpha must be opaque."); return false; }
                    }
                    if (image.width != thumbnail_default_size || image.height != thumbnail_default_size || brightest - darkest < 30)
                    { stop("GPU preview is blank or has invalid dimensions."); return false; }
                    const auto output = VirtualPath::parse(name + ".png");
                    const auto saved = workspace_.files().write_binary_atomic(output.value(), thumbnail.value().png, FilePublishMode::Replace);
                    if (!saved.succeeded()) { stop(saved.message); return false; }
                    found_thumbnail = true;
                }
            }
            if (!found_thumbnail || !found_opaque) { stop("Thumbnail persistence lost required test segments."); return false; }
            return true;
        }
        void stop(std::string error) { state_.error = std::move(error); window().close(); }
        bool verify_asset_placement()
        {
            AssetPlacementRequest request;
            request.asset_id = first_;
            request.on_ground = true;
            std::string error;
            const auto id = place_static_mesh_asset(workspace_, world(), factory_, history_, request, error);
            Actor* actor = world().find_actor_by_id(id);
            if (!actor || actor->root_component()->local_transform().translation.y != 1 ||
                !history_.undo(world()) || world().actor_count() != 0 || !history_.redo(world()) || world().actor_count() != 1)
            { stop("Asset placement, imported ground offset or undo/redo failed: " + error); return false; }
            request.asset_id = {};
            if (place_static_mesh_asset(workspace_, world(), factory_, history_, request, error) || world().actor_count() != 1)
            { stop("A stale or invalid asset identity mutated the scene."); return false; }
            if (!history_.undo(world()) || world().actor_count() != 0)
            { stop("Rejected asset placement changed the command history."); return false; }
            history_.clear();
            return true;
        }
#if WITH_WIN64
        bool verify_native_drop()
        {
            auto* native = dynamic_cast<Win32Window*>(&window());
            if (!native || !window().enable_file_drop(true)) { stop("Native file drop could not be enabled."); return false; }
            const wchar_t first[] = L"D:\\mesh.obj";
            const wchar_t second[] = L"D:\\模型.fbx";
            const std::size_t bytes = sizeof(DROPFILES) + sizeof(first) + sizeof(second) + sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            void* data = memory ? GlobalLock(memory) : nullptr;
            if (!data) { if (memory) GlobalFree(memory); stop("Native drop fixture allocation failed."); return false; }
            DROPFILES drop{};
            drop.pFiles = sizeof(DROPFILES);
            drop.pt = POINT{140, 90};
            drop.fWide = TRUE;
            std::memcpy(data, &drop, sizeof(drop));
            auto* names = static_cast<unsigned char*>(data) + sizeof(DROPFILES);
            std::memcpy(names, first, sizeof(first));
            std::memcpy(names + sizeof(first), second, sizeof(second));
            GlobalUnlock(memory);
            // WM_DROPFILES transfers ownership; Window's DragFinish releases it.
            SendMessageW(native->get_native_hwnd(), WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
            FileDropEvent event;
            if (!window().take_file_drop(event) || event.position != Vector2(140, 90) || event.paths.size() != 2 ||
                event.paths[0] != "D:\\mesh.obj" || event.paths[1] != u8"D:\\模型.fbx" || window().take_file_drop(event))
            { stop("Native drop did not preserve owned UTF-8 paths and client coordinates."); return false; }
            if (!window().enable_file_drop(false)) { stop("Native drop disable failed."); return false; }
            return true;
        }
#endif
        void on_build_ui() override
        {
            ImGui::SetNextWindowSize(ImVec2(620, 320), ImGuiCond_Always);
            draw_content_browser(workspace_, selection_, folder_, show_engine_, pool_, tile_size_);
            ImGui::SetNextWindowSize(ImVec2(620, 320), ImGuiCond_Always);
            ImGui::Begin("Thumbnail integration");
            for (const auto& asset : workspace_.catalog().entries)
            {
                const auto view = pool_.request(asset);
                if (view.texture_id.valid())
                {
                    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(view.texture_id.value())), ImVec2(256, 256));
                    ImGui::SameLine();
                }
            }
            ImGui::End();
            if (!import_dialog_shown_)
            {
                import_dialog_shown_ = true;
                if (!import_dialog_.request("/Project", {"not-confirmed.obj"}))
                { stop("Import confirmation dialog did not open."); return; }
            }
            // Draw the actual settings modal at a small window size. Requesting
            // or drawing it must never import before an explicit confirmation.
            import_dialog_.draw(window(), workspace_, selection_, pool_);
            const auto unconfirmed = VirtualPath::parse("/Project/not-confirmed.asset");
            const auto state = workspace_.files().stat(unconfirmed.value());
            if (state.succeeded() || state.status().code != FileErrorCode::NotFound)
                stop("Opening the import dialog wrote an unconfirmed asset.");
        }
        void on_shutdown() override
        {
            import_dialog_.clear();
            pool_.shutdown();
            if (!flush_rendering_commands().succeeded()) state_.error = "Preview teardown did not drain.";
            factory_.release();
        }

        EditorWorkspace& workspace_;
        AssetThumbnailPool pool_;
        ActorFactory factory_;
        EditorCommandHistory history_;
        EditorSelection selection_;
        StaticMeshImportDialog import_dialog_;
        bool import_dialog_shown_ = false;
        std::string folder_ = "/Project";
        bool show_engine_ = false;
        float tile_size_ = 112;
        AssetId first_;
        AssetId second_;
        TestState& state_;
        Sha256Hash conflict_snapshot_{};
        bool started_ = false;
        int phase_ = 0;
        double elapsed_ = 0;
    };
}

int main()
{
    using namespace toy3d;
    std::string destination = "unchanged", error;
    if (!static_mesh_import_destination("C:/模型.FBX", "/Project/Models", u8"模型", 1, destination, error) ||
        destination != u8"/Project/Models/模型.asset" ||
        static_mesh_import_destination("a.obj", "/Engine", "mesh", 1, destination, error) ||
        static_mesh_import_destination("a.obj", "/Project2", "mesh", 1, destination, error) ||
        static_mesh_import_destination("a.png", "/Project", "mesh", 1, destination, error) ||
        static_mesh_import_destination("a.obj", "/Project", "../mesh", 1, destination, error) ||
        static_mesh_import_destination("a.obj", "/Project", "mesh", 0, destination, error))
    { std::cerr << "Import settings validation failed: " << error; return EXIT_FAILURE; }
    StaticMeshImportDialog dialog;
    if (dialog.request("/Engine", {"a.obj"}) || dialog.active() ||
        dialog.request("/Project", std::vector<std::string>(maximum_file_drop_paths + 1, "a.obj")) ||
        !dialog.request("/Project", {"a.obj"}) || !dialog.active() || dialog.request("/Project", {"b.obj"}))
    { std::cerr << "Import transaction boundary failed."; return EXIT_FAILURE; }
    dialog.clear();
    if (dialog.active()) return EXIT_FAILURE;
    NativePlatformFile platform;
    AssetId first, second;
    if (!AssetId::try_generate(first) || !AssetId::try_generate(second)) return EXIT_FAILURE;
    const auto root = platform.join_relative(PhysicalPath(TOY3D_THUMBNAIL_TEST_ROOT), first.hex());
    if (!root.succeeded() || !platform.create_directories(root.value()).succeeded()) return EXIT_FAILURE;
    EditorWorkspace workspace;
    EditorWorkspacePaths paths;
    paths.project_assets = root.value();
    paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
    paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
    if (!workspace.initialize(paths)) { std::cerr << workspace.error(); return EXIT_FAILURE; }
    StaticMeshAssetGeometry geometry;
    const std::vector<Vector3> positions{{-1,-1,-1}, {1,-1,-1}, {0,1,-1}, {0,0,1}};
    for (const auto& position : positions)
    {
        Vector3 normal;
        if (!try_normalize(position, normal)) return EXIT_FAILURE;
        geometry.vertices.push_back({position, normal, Vector2(0)});
    }
    geometry.indices = {0,2,1,0,1,3,0,3,2,1,2,3};
    geometry.sections = {{0, 12, 0}};
    geometry.material_slots = {"Preview"};
    const auto a = encode_static_mesh_asset(first, geometry, {{"future_editor_data", 47, false, {1,2,3}}});
    const auto b = encode_static_mesh_asset(second, geometry, {{"future_editor_data", 47, false, {1,2,3}}});
    const auto path_a = VirtualPath::parse("/Project/first.asset");
    const auto path_b = VirtualPath::parse("/Project/second.asset");
    if (!a.succeeded() || !b.succeeded() ||
        !workspace.files().write_binary_atomic(path_a.value(), a.value(), FilePublishMode::CreateNew).succeeded() ||
        !workspace.files().write_binary_atomic(path_b.value(), b.value(), FilePublishMode::CreateNew).succeeded() ||
        !workspace.refresh()) return EXIT_FAILURE;
#if WITH_MODEL_IMPORT
    const auto source = platform.join_relative(root.value(), "import-smoke.obj");
    if (!source.succeeded() || !platform.write_text_utf8(source.value(),
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", FileWriteMode::CreateNew).succeeded()) return EXIT_FAILURE;
    AssetId imported;
    StaticMeshImportOptions options;
    const std::size_t before = workspace.catalog().entries.size();
    if (!import_static_mesh_to_workspace(workspace, source.value(), "/Project/import-smoke.asset", options, imported, error) ||
        !imported.valid() || workspace.catalog().entries.size() != before + 1) return EXIT_FAILURE;
    AssetId rejected;
    if (import_static_mesh_to_workspace(workspace, source.value(), "/Project/import-smoke.asset", options, rejected, error) ||
        rejected.valid() || import_static_mesh_to_workspace(workspace, source.value(), "/Engine/import-smoke.asset", options, rejected, error))
    { std::cerr << "Import overwrite or read-only boundary failed."; return EXIT_FAILURE; }
    const auto imported_path = VirtualPath::parse("/Project/import-smoke.asset");
    if (!workspace.files().remove_file(imported_path.value()).succeeded() || !workspace.refresh()) return EXIT_FAILURE;
#endif
    CommandLineParser::get_instance().parser_args({"ThumbnailTests", "--Window.Width=720", "--Window.Height=480", "--Window.Title=Thumbnail Tests"});
    TestState state;
    {
        Engine engine;
        ShaderLoadConfig config;
        config.mode = ShaderLoadMode::ShaderMapEntry;
        config.path = PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT);
        engine.set_shader_load_config(std::move(config));
        engine.set_application(std::make_unique<ThumbnailTestApplication>(workspace, first, second, state));
#if WITH_WIN64
        engine.init(static_cast<void*>(GetModuleHandleW(nullptr)));
#else
        engine.init(nullptr);
#endif
        engine.main_loop();
        engine.exit();
    }
    std::cout << "Thumbnail artifacts: " << root.value().utf8() << '\n';
    if (!state.complete || !state.error.empty()) { std::cerr << "Thumbnail integration failed: " << state.error << '\n'; return EXIT_FAILURE; }
    std::cout << "Preview render, PNG persistence, opaque preservation, multi-image reload and save conflict passed.\n";
    return EXIT_SUCCESS;
}
