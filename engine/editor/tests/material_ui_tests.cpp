#include "asset_loader/asset_loader.h"
#include "rendercore/texture/texture_load_job.h"
#include "asset/texture/builtin_texture_assets.h"
#include "assets/material/material_editor_panel.h"
#include "assets/asset_resource_picker.h"
#include "assets/preview/preview_scene_widgets.h"
#include "panels/editor_panel_registry.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "imgui.h"
#include "imgui_internal.h"

#include "rendercore/rendering_thread.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"
#include "shader/shader_format_types.h"
#include "threading/task_graph/task_graph.h"
#include "threading/thread_manager.h"
#include "workspace/editor_workspace.h"

namespace
{
    int failures = 0;
    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    }

    void frame(toy3d::MaterialEditorPanel& panel, toy3d::EditorPanelRegistry& panels)
    {
        ImGui::NewFrame();
        panels.draw();
        panels.process_shortcuts(panel.modal_pending());
        ImGui::Render();
    }

    void dismiss_popup()
    {
        ImGui::NewFrame();
        if (ImGui::BeginPopupModal("Unsaved Material"))
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Render();
    }

    void test_preview_scene_controls(const toy3d::EditorWorkspace& workspace)
    {
        using namespace toy3d;
        MaterialPreviewSettings material;
        material.mesh = MaterialPreviewMesh::Plane;
        material.camera_distance = 700;
        material.scene.show_floor = false;
        material.scene.exposure_ev = 2;
        PreviewSceneSettings animation;
        animation.light_intensity = 4;
        const auto animation_before = animation;
        ImVec2 reset_position;
        const auto draw = [&]()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(600, 750), ImGuiCond_Always);
            ImGui::Begin("Shared Preview Scene Controls", nullptr, ImGuiWindowFlags_NoSavedSettings);
            draw_preview_scene_settings(workspace, material.scene);
            const auto minimum = ImGui::GetItemRectMin();
            const auto maximum = ImGui::GetItemRectMax();
            reset_position = ImVec2((minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f);
            ImGui::End();
            ImGui::Render();
        };
        draw();
        draw();
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(reset_position.x, reset_position.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        draw();
        check(material.scene == PreviewSceneSettings{},
              "Shared Reset Scene restores environment, light and floor defaults");
        check(material.mesh == MaterialPreviewMesh::Plane && material.camera_distance == 700,
              "Shared scene controls preserve material model and camera");
        check(animation == animation_before, "Changing one preview leaves another window's settings intact");
    }

    void test_texture_picker(toy3d::AssetResourcePicker& picker, const toy3d::EditorWorkspace& workspace,
                             const toy3d::AssetId& incompatible)
    {
        using namespace toy3d;
        AssetId texture;
        AssetId other_texture;
        AssetId::parse("a6a53651e980491294a675d094006da0", texture);
        AssetId::parse("26e14823067241ee84676de813b2e8c3", other_texture);
        check(workspace.catalog().index.find(texture) && workspace.catalog().index.find(other_texture),
              "texture picker fixtures exist in the real engine catalog");
        AssetId selected = texture;
        AssetId browsed;
        picker.set_selected_asset(
            [&]()
            {
                return selected;
            });
        picker.set_browse(
            [&](const AssetId& id)
            {
                browsed = id;
            });
        AssetResourceSelection current{other_texture, {}};
        std::string error;
        ImVec2 thumbnail;
        ImVec2 use;
        ImVec2 clear;
        ImVec2 find;
        ImVec2 name;
        const auto draw = [&](const char* payload_type = nullptr, const AssetId* payload_asset = nullptr)
        {
            ImGui::NewFrame();
            if (payload_type && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern))
            {
                ImGui::SetDragDropPayload(payload_type, payload_asset, sizeof(AssetId));
                ImGui::TextUnformatted("Texture fixture");
                ImGui::EndDragDropSource();
            }
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(500, 180), ImGuiCond_Always);
            ImGui::Begin("Texture picker interaction", nullptr, ImGuiWindowFlags_NoSavedSettings);
            const auto& style = ImGui::GetStyle();
            AssetResourceSelection next;
            const bool changed = picker.draw("Texture", workspace, current, {"toy3d.Texture2DAssetData"}, next, error);
            ImGui::PushID("Texture");
            ImGui::PushID("Texture");
            const auto* table = ImGui::GetCurrentContext()->Tables.GetByKey(ImGui::GetID("##Property"));
            ImGui::PopID();
            ImGui::PopID();
            if (table)
            {
                const float x = table->Columns[1].WorkMinX;
                const float y = table->RowPosY1 + style.CellPadding.y;
                const float frame = ImGui::GetFrameHeight();
                const float size = frame * 2.0f + style.ItemSpacing.y;
                thumbnail = ImVec2(x + size * 0.5f, y + size * 0.5f);
                name = ImVec2(x + size + style.ItemSpacing.x + 8, y + frame * 0.5f);
                use = ImVec2(name.x - 8 + frame * 0.5f, y + frame + style.ItemSpacing.y + frame * 0.5f);
                find = ImVec2(use.x + frame + 2, use.y);
                clear = ImVec2(find.x + frame + 2, use.y);
            }
            if (changed)
            {
                current = next;
            }
            ImGui::End();
            ImGui::Render();
            return changed;
        };
        auto& io = ImGui::GetIO();
        const auto click = [&](ImVec2 position)
        {
            io.AddMousePosEvent(position.x, position.y);
            draw();
            io.AddMouseButtonEvent(0, true);
            draw();
            io.AddMouseButtonEvent(0, false);
            return draw();
        };
        draw();
        check(click(use) && current.asset == texture, "Use assigns the compatible Content Browser selection");
        check(!click(find) && browsed == texture, "Find locates the current reference without changing it");
        check(click(clear) && !current.asset.valid(), "Clear returns an empty selection to restore caller defaults");
        current.asset = other_texture;
        selected = incompatible;
        check(!click(use) && current.asset == other_texture, "Use cannot assign a Material to a Texture2D field");
        click(thumbnail);
        check(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
              "clicking the thumbnail opens the searchable texture picker");
        click(ImVec2(510, 10));
        draw();
        check(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
              "clicking outside dismisses the texture picker before drag/drop tests");
        const auto drop = [&](ImVec2 target, const char* payload_type, const AssetId& asset)
        {
            io.AddMousePosEvent(target.x, target.y);
            draw();
            io.AddMouseButtonEvent(0, true);
            draw(payload_type, &asset);
            draw(payload_type, &asset);
            io.AddMouseButtonEvent(0, false);
            return draw(payload_type, &asset);
        };
        check(drop(thumbnail, "TOY3D_TEXTURE_ASSET", texture) && current.asset == texture,
              "Texture payload delivers onto the thumbnail itself");
        check(drop(name, "TOY3D_TEXTURE_ASSET", other_texture) && current.asset == other_texture,
              "Texture payload delivers onto the resource name");
        check(!drop(thumbnail, "TOY3D_ASSET", incompatible) && current.asset == other_texture && !error.empty(),
              "incompatible drop diagnoses and preserves the current texture");
        picker.set_selected_asset({});
        picker.set_browse({});
    }
} // namespace

int main()
{
    using namespace toy3d;
    NativePlatformFile platform;
    AssetId root_id;
    AssetId child_id;
    if (!AssetId::try_generate(root_id) || !AssetId::try_generate(child_id))
    {
        return 1;
    }
    EditorWorkspacePaths paths;
    paths.project_assets = PhysicalPath(std::string(TOY3D_MATERIAL_TEST_ROOT) + "/" + root_id.hex());
    paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
    paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
    if (!platform.create_directories(paths.project_assets).succeeded())
    {
        return 1;
    }
    EditorWorkspace workspace;
    if (!workspace.initialize(paths))
    {
        std::cerr << workspace.error();
        return 1;
    }
    MaterialAssetData root;
    root.shader_name = "Toy3d/Surface/Phong";
    const auto bytes = encode_material_asset_pair(workspace.types(), root_id, root);
    const auto root_path = VirtualPath::parse("/Project/M_Ui.asset");
    const auto child_path = VirtualPath::parse("/Project/MI_Ui.asset");
    if (!bytes.succeeded() || !root_path.succeeded() || !child_path.succeeded())
    {
        return 1;
    }
    if (!workspace.asset_pairs().publish(root_path.value(), bytes.value(), FilePublishMode::CreateNew).succeeded() ||
        !workspace.refresh())
    {
        return 1;
    }
    MaterialInstanceAssetData child;
    child.parent.asset_id = root_id;
    child.parent.expected_type = "toy3d.MaterialAssetData";
    const auto child_bytes =
        encode_material_instance_asset_pair(workspace.types(), child_id, child, &workspace.catalog().index);
    if (!child_bytes.succeeded() ||
        !workspace.asset_pairs()
             .publish(child_path.value(), child_bytes.value(), FilePublishMode::CreateNew)
             .succeeded() ||
        !workspace.refresh())
    {
        return 1;
    }
    ThreadManager threads;
    auto graph_result = create_task_graph({0u, 256u, false}, threads);
    if (!graph_result.succeeded())
    {
        return 1;
    }
    auto graph = graph_result.take_task_graph();
    if (!graph->attach_to_thread(NamedThread::GameThread).succeeded())
    {
        return 1;
    }
    RenderingThread rendering(threads, *graph, RenderingThreadMode::SingleThread);
    if (!rendering.start().succeeded())
    {
        return 1;
    }
    ShaderMapEntryLoader entry_loader(PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
    ShaderMap map(entry_loader);
    ShaderMapProgramKey key;
    key.shader_name = root.shader_name;
    key.pass_name = "Forward";
    key.role = shader::ShaderPassRole::Forward;
    key.vertex_factory = shader::VertexFactoryType::Local;
    const auto program =
        ShaderMapCollection::create_candidate(entry_loader.load_default_collection(key.shader_name, key.platform));
    if (!program.succeeded())
    {
        std::cerr << program.error;
        return 1;
    }
    TextureDesc white;
    white.width = 1u;
    white.height = 1u;
    white.format = PixelFormat::R8G8B8A8UNorm;
    white.row_pitches = {4u};
    white.slice_pitches = {4u};
    white.mip_pixels = {{255u, 255u, 255u, 255u}};
    TextureRef texture = Texture::create(std::move(white));
    MaterialInstanceRef defaults;
    {
        MaterialTextureValues textures;
        textures.named_defaults.emplace("white", texture);
        const auto built = create_material_from_asset(root, program.collection, textures);
        if (!built.succeeded())
        {
            std::cerr << built.status().message;
            return 1;
        }
        defaults = built.value();
    }
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1024, 768);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* font_pixels = nullptr;
    int font_width = 0;
    int font_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    check(font_pixels && font_width > 0 && font_height > 0, "ImGui font atlas");
    MaterialEditorPanel panel;
    // Engine teardown also runs when initialization failed before a session existed.
    panel.shutdown();
    panel.initialize(workspace, defaults->material(), PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
    AssetThumbnailPool thumbnails(workspace);
    AssetResourcePicker picker(thumbnails);
    // The loader outlives every consumer that holds it; the panel is injected here so its
    // texture slots decode off the Game Thread like the preview windows do.
    AssetLoader loader;
    check(loader.initialize(workspace.files(), threads), "asset loader thread must start");
    panel.set_asset_loader(loader);
    // The shared loader hands every consumer the same adopted Texture and drops it on
    // invalidation instead of letting each owner decode its own copy.
    {
        // A missing identity must fail, and the failure must be remembered rather than retried.
        AssetRef reference;
        check(AssetId::parse(std::string(32u, '1'), reference.asset_id), "removed identity fixture must parse");
        reference.expected_type = "toy3d.EnvironmentAssetData";
        const auto miss = request_texture(loader, reference, workspace.catalog().index, AssetLoadPriority::High);
        check(!miss.ready() && miss.pending(), "an unknown identity must start a decode");
        // Decoding is asynchronous; adoption follows on a later tick, exactly as a frame does.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (miss.pending() && std::chrono::steady_clock::now() < deadline)
        {
            loader.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(miss.failed() && !miss.error().empty(),
              "a removed identity must report its decode failure instead of an empty image");
        check(loader.cached_entries() == 0u, "a failed decode must not repopulate the cache");
        // The failure is remembered per identity: a consumer polling every frame must observe
        // the error instead of restarting the same failing decode.
        const auto repeated = request_texture(loader, reference, workspace.catalog().index, AssetLoadPriority::High);
        check(repeated.failed() && repeated.error() == miss.error(),
              "a failed decode must be reported again without being repeated");
        // The type table decides the tier when the caller states none, so cube and ordinary
        // texture loads cannot end up on inconsistent tiers across windows.
        check(default_asset_load_priority("toy3d.EnvironmentAssetData") == AssetLoadPriority::High,
              "an environment cube must default to the High tier");
        check(default_asset_load_priority("toy3d.TextureAssetData") == AssetLoadPriority::Normal,
              "an ordinary texture must default to the Normal tier");
        // A polling consumer drops its handle every frame; that must not cancel the shared
        // decode, so the finished cube still reaches the cache for the next request.
        AssetRef environment;
        check(AssetId::parse(builtin_studio_environment_id, environment.asset_id),
              "studio environment identity must parse");
        environment.expected_type = "toy3d.EnvironmentAssetData";
        const auto request_start = std::chrono::steady_clock::now();
        request_texture(loader, environment, workspace.catalog().index);
        std::cout << "AssetLoader request (Game Thread): "
                  << std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - request_start).count()
                  << " us for a 4 MB cube face that decodes on the loader thread" << std::endl;
        const auto environment_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (loader.cached_entries() == 0u && std::chrono::steady_clock::now() < environment_deadline)
        {
            loader.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(loader.cached_entries() == 1u, "a dropped handle must not cancel the shared decode");
        const auto environment_hit = request_texture(loader, environment, workspace.catalog().index);
        check(environment_hit.ready(), "a completed shared decode must serve the next request from the cache");
        const std::size_t single_identity_bytes = loader.cached_bytes();
        // A candidate invalidated while its decode is in flight must release its waiter with a
        // retry state instead of leaving the handle pending forever.
        loader.invalidate(environment.asset_id);
        const auto stranded = request_texture(loader, environment, workspace.catalog().index);
        loader.invalidate(environment.asset_id);
        loader.tick();
        check(stranded.invalidated() && !stranded.pending(),
              "an invalidated in-flight candidate must release its waiter instead of hanging");
        const auto refreshed = request_texture(loader, environment, workspace.catalog().index);
        const auto refresh_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (refreshed.pending() && std::chrono::steady_clock::now() < refresh_deadline)
        {
            loader.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(refreshed.ready(), "a request after invalidation must be resolved by a fresh decode");
        check(loader.cached_bytes() == single_identity_bytes && loader.cached_entries() == 1u,
              "a re-decoded identity must be billed once, not once per decode");
        // Assembly paths wait for a Critical decode instead of polling; a resolved handle must
        // come back true and a missing identity must time out instead of blocking forever.
        const auto assembly_start = std::chrono::steady_clock::now();
        auto assembly = request_texture(loader, environment, workspace.catalog().index, AssetLoadPriority::Critical);
        const bool assembly_ready = loader.wait(assembly, std::chrono::seconds(5));
        std::cout
            << "Assembly wait (identity a preview already decoded): "
            << std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - assembly_start).count()
            << " us" << std::endl;
        check(assembly_ready && assembly.ready() && assembly.get() == refreshed.get(),
              "assembly must reuse the texture a preview already decoded");
        // A catalog rescan invalidates everything: the cache is dropped and an in-flight waiter
        // is released with the retry state instead of staying pending.
        loader.invalidate(environment.asset_id);
        const auto wholed = request_texture(loader, environment, workspace.catalog().index);
        loader.invalidate_all();
        check(wholed.invalidated() && !wholed.pending(),
              "invalidate_all must release an in-flight waiter instead of leaving it pending");
        check(loader.cached_entries() == 0u, "invalidate_all must drop every cached identity");
        AssetId missing;
        check(AssetId::parse(std::string(32u, 'f'), missing), "missing identity fixture must parse");
        AssetRef absent;
        absent.asset_id = missing;
        absent.expected_type = "toy3d.EnvironmentAssetData";
        auto timed_out = request_texture(loader, absent, workspace.catalog().index, AssetLoadPriority::Critical);
        check(!loader.wait(timed_out, std::chrono::milliseconds(500)) && timed_out.failed(),
              "an assembly wait must report a decode failure instead of blocking forever");
        // The cache is keyed by identity while a typed handle downcasts its job, so a second entry
        // point asking for the same identity as another kind must be refused, not cast blindly.
        loader.invalidate_all();
        auto as_environment = request_texture(loader, environment, workspace.catalog().index);
        loader.wait(as_environment, std::chrono::seconds(30));
        check(as_environment.ready(), "the type-conflict fixture must cache its identity first");
        AssetRef mislabelled = environment;
        mislabelled.expected_type = "toy3d.StaticMeshAssetData";
        const auto mismatched = request_texture(loader, mislabelled, workspace.catalog().index);
        check(mismatched.failed() && mismatched.error().find("requested as") != std::string::npos,
              "the same identity requested as another kind must fail instead of casting the cached job");
    }
    panel.set_resource_picker(picker);
    test_preview_scene_controls(workspace);
    test_texture_picker(picker, workspace, root_id);
    EditorPanelRegistry panels;
    check(panels.add({"material", "Material Editor", "Material Editor",
                      [&]()
                      {
                          panel.draw();
                      },
                      [&]()
                      {
                          panel.undo();
                      },
                      [&]()
                      {
                          panel.redo();
                      },
                      [&]()
                      {
                          return panel.focused();
                      },
                      [&]()
                      {
                          panel.save();
                      }}),
          "Material panel registers command callbacks");
    panels.freeze();
    panel.request_open(root_id);
    frame(panel, panels);
    auto& session = panel.edit_session();
    check(session.active() && session.id() == root_id && ImGui::GetDrawData()->TotalVtxCount > 0,
          "real material panel opens and submits controls");
    check(session.begin_gesture().succeeded() && session.set_parameter({"specular_power", 80.0f}).succeeded(),
          "UI adapter accepts a draft batch");
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame(panel, panels);
    check(!session.gesturing() && !session.dirty() && session.undo_count() == 0u,
          "Escape routed by actual panel cancels draft without history");
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame(panel, panels);
    check(session.set_parameter({"specular_power", 64.0f}).succeeded(), "root edit through active adapter");
    panel.request_open(child_id);
    frame(panel, panels);
    check(session.id() == root_id && session.dirty() && panel.modal_pending(), "dirty switch defers and shows modal");
    check(panel.resolve_unsaved(MaterialCloseDecision::Cancel) && session.id() == root_id && session.dirty(),
          "Cancel retains old session and draft");
    dismiss_popup();
    panel.request_open(child_id);
    frame(panel, panels);
    check(panel.resolve_unsaved(MaterialCloseDecision::Save) && session.id() == child_id && !session.dirty(),
          "Save then switch loads child inheriting saved parent");
    dismiss_popup();
    const auto effective = session.effective_overrides();
    bool inherited = false;
    for (const auto& value : effective)
    {
        // C++17 get_if verifies the inherited property's exact persisted shape.
        if (value.name == "specular_power")
        {
            if (const auto* number = std::get_if<float>(&value.value))
            {
                inherited = *number == 64.0f;
            }
        }
    }
    check(inherited && session.overrides().empty(), "child inherits parent without serializing copied values");
    {
        AssetId alternate_id;
        AssetId grand_id;
        check(AssetId::try_generate(alternate_id) && AssetId::try_generate(grand_id), "allocate Parent edit fixtures");
        MaterialAssetData alternate = root;
        alternate.overrides = {{"specular_power", 24.0f}};
        const auto alternate_path = VirtualPath::parse("/Project/M_Alternate.asset");
        const auto alternate_bytes = encode_material_asset_pair(workspace.types(), alternate_id, alternate);
        check(alternate_bytes.succeeded() &&
                  workspace.asset_pairs()
                      .publish(alternate_path.value(), alternate_bytes.value(), FilePublishMode::CreateNew)
                      .succeeded() &&
                  workspace.refresh(),
              "publish alternate Parent");
        MaterialInstanceAssetData grand;
        grand.parent = {child_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
        const auto grand_path = VirtualPath::parse("/Project/MI_UiGrand.asset");
        const auto grand_bytes =
            encode_material_instance_asset_pair(workspace.types(), grand_id, grand, &workspace.catalog().index);
        check(grand_bytes.succeeded() &&
                  workspace.asset_pairs()
                      .publish(grand_path.value(), grand_bytes.value(), FilePublishMode::CreateNew)
                      .succeeded() &&
                  workspace.refresh(),
              "publish descendant for cycle validation");
        const AssetRef next{alternate_id, {}, "toy3d.MaterialAssetData", AssetRefStrength::Strong};
        check(session.set_parent(next).succeeded() && session.instance_data()->parent.asset_id == alternate_id &&
                  session.dirty(),
              "Parent switch uses full preview candidate and records one asset edit");
        check(session.parameter_source("specular_power").asset_id == alternate_id && session.overrides().empty(),
              "source identifies the effective Parent without copying values");
        check(session.undo().succeeded() && session.instance_data()->parent.asset_id == root_id && !session.dirty(),
              "Parent undo restores original hierarchy and clean checkpoint");
        check(session.redo().succeeded() && session.instance_data()->parent.asset_id == alternate_id &&
                  session.undo().succeeded(),
              "Parent redo rebuilds the validated candidate");
        const auto undo_count = session.undo_count();
        const AssetRef descendant{grand_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
        check(!session.set_parent(descendant).succeeded() && session.undo_count() == undo_count && !session.dirty() &&
                  session.instance_data()->parent.asset_id == root_id,
              "indirect Parent cycle preserves preview, history and file");
        const AssetRef self{child_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
        check(!session.set_parent(self).succeeded(), "self Parent is rejected");
    }
    const auto scalar_start = std::chrono::steady_clock::now();
    const bool scalar_draft = session.set_parameter({"specular_power", 100.0f}).succeeded();
    std::cout << "Material scalar slot: Game Thread commit "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - scalar_start).count()
              << " ms for comparison with the texture slot above" << std::endl;
    check(scalar_draft, "child draft");
    frame(panel, panels);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_Z, true);
    frame(panel, panels);
    check(session.overrides().empty() && !session.dirty(), "Ctrl+Z routed to the asset history");
    io.AddKeyEvent(ImGuiKey_Z, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame(panel, panels);
    check(session.set_parameter({"specular_power", 90.0f}).succeeded() && !panel.request_exit(),
          "dirty application close defers to save prompt");
    frame(panel, panels);
    check(panel.resolve_unsaved(MaterialCloseDecision::Cancel) && !panel.take_exit() && session.dirty(),
          "Cancel exit preserves unsaved asset");
    dismiss_popup();
    check(!panel.request_exit(), "second dirty exit request");
    frame(panel, panels);
    check(panel.resolve_unsaved(MaterialCloseDecision::Discard) && panel.take_exit() && !session.active(),
          "Discard clears runtime and session before admitting exit");
    dismiss_popup();
    check(panel.request_exit(), "clean exit is admitted");
    panel.request_open(child_id);
    frame(panel, panels);
    int publications = 0;
    session.set_publish(
        [&publications](const AssetRef&) -> AssetStatus
        {
            ++publications;
            if (publications == 1)
            {
                return {AssetErrorCode::InvalidState, {}, {}, {}, {}, "candidate publication failed", {}};
            }
            return AssetStatus::success();
        });
    check(session.set_parameter({"specular_power", 72.0f}).succeeded(), "prepare save/publication failure fixture");
    check(!panel.request_exit(), "publication failure during close opens the save prompt");
    frame(panel, panels);
    check(!panel.resolve_unsaved(MaterialCloseDecision::Save) && !panel.take_exit() && session.active(),
          "failed publication keeps the session and error visible after successful file save");
    frame(panel, panels);
    check(session.active() && panel.modal_pending() && !panel.take_exit(),
          "next UI frame cannot silently close a clean session after publication failure");
    MaterialInstanceAssetData saved_child;
    check(!session.dirty() && publications == 1 &&
              read_material_instance_asset(workspace.types(), workspace.files(), child_path.value(), saved_child)
                  .succeeded() &&
              saved_child.overrides.size() == 1u,
          "saved file remains committed when rendering publication fails");
    check(session.publish_saved().succeeded() && publications == 2 && !session.dirty(),
          "Retry Publish does not rewrite or dirty the saved asset");
    check(panel.resolve_unsaved(MaterialCloseDecision::Cancel) && !panel.take_exit(),
          "cancel pending close after publication retry");
    dismiss_popup();
    // A texture slot decodes through the shared loader, but building the runtime material needs
    // the image present: the commit must resolve and cache it before it returns, otherwise the
    // builder rejects the batch with "Texture asset is not loaded for parameter".
    if (defaults->desc().shader_map)
    {
        std::string texture_parameter;
        for (const auto& resource : defaults->desc().shader_map->programs().front()->data().parameter_schema.resources)
        {
            if (resource.category == shader::ShaderParameterCategory::SampledTexture &&
                resource.resource_kind == shader::ResourceKind::Texture2D &&
                resource.texture_usage == TextureUsage::Color)
            {
                texture_parameter = resource.name;
                break;
            }
        }
        check(!texture_parameter.empty(), "the Phong schema must expose a sampled texture parameter");
        AssetRef white;
        check(AssetId::parse(builtin_texture_assets[0].asset_id, white.asset_id),
              "builtin texture identity must parse");
        white.expected_type = "toy3d.Texture2DAssetData";
        const MaterialParameterOverride bound{texture_parameter, white};
        const std::size_t entries_before = loader.cached_entries();
        const auto commit_start = std::chrono::steady_clock::now();
        const auto committed = session.set_parameter(bound);
        const auto commit_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - commit_start).count();
        check(committed.succeeded(), "a texture batch must resolve its image and commit");
        bool recorded = false;
        for (const auto& value : session.overrides())
        {
            recorded = recorded || value.name == texture_parameter;
        }
        check(recorded, "the committed texture override must be recorded");
        std::cout << "Material texture slot: Game Thread commit " << commit_ms
                  << " ms including the loader wait; the decode itself ran on the loader thread" << std::endl;
        check(loader.cached_entries() == entries_before + 1u,
              "a texture batch must resolve and cache its image before the runtime material is built");
        check(session.active() && session.overrides().size() == 2u,
              "the committed batch must keep both the scalar and the texture override");
    }
    session.set_publish({});
    panels.clear();
    panel.shutdown();
    panel.shutdown();
    loader.shutdown();
    ImGui::DestroyContext();
    MaterialInstance::release(defaults);
    check(texture.use_count() == 1u, "window closes release every shared default texture reference");
    texture.reset();
    check(rendering.stop().succeeded(), "facade shutdown");
    check(graph->shutdown(TaskGraphShutdownMode::Drain).succeeded(), "graph shutdown");
    std::cout << (failures ? "Material UI tests failed\n" : "Material UI and close decisions passed\n");
    return failures ? 1 : 0;
}
