#include "assets/material/material_editor_panel.h"
#include "panels/editor_panel_registry.h"

#include <iostream>
#include <memory>

#include "imgui.h"
#include "imgui_internal.h"

#include "rendercore/rendering_thread.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"
#include "threading/task_graph/task_graph.h"
#include "threading/thread_manager.h"
#include "workspace/editor_workspace.h"

namespace
{
    int failures = 0;
    void check(bool value, const char* message)
    {
        if (!value) { std::cerr << "FAILED: " << message << '\n'; ++failures; }
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
}

int main()
{
    using namespace toy3d;
    NativePlatformFile platform;
    AssetId root_id; AssetId child_id;
    if (!AssetId::try_generate(root_id) || !AssetId::try_generate(child_id)) return 1;
    EditorWorkspacePaths paths;
    paths.project_assets = PhysicalPath(std::string(TOY3D_MATERIAL_TEST_ROOT) + "/" + root_id.hex());
    paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
    paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
    if (!platform.create_directories(paths.project_assets).succeeded()) return 1;
    EditorWorkspace workspace;
    if (!workspace.initialize(paths)) { std::cerr << workspace.error(); return 1; }
    MaterialAssetData root;
    root.shader_name = "Toy3d/Surface/Phong";
    const auto bytes = encode_material_asset_pair(workspace.types(), root_id, root);
    const auto root_path = VirtualPath::parse("/Project/M_Ui.asset");
    const auto child_path = VirtualPath::parse("/Project/MI_Ui.asset");
    if (!bytes.succeeded() || !root_path.succeeded() || !child_path.succeeded()) return 1;
    if (!workspace.asset_pairs().publish(root_path.value(), bytes.value(), FilePublishMode::CreateNew).succeeded() ||
        !workspace.refresh()) return 1;
    MaterialInstanceAssetData child;
    child.parent.asset_id = root_id;
    child.parent.expected_type = "toy3d.MaterialAssetData";
    const auto child_bytes = encode_material_instance_asset_pair(workspace.types(), child_id, child, &workspace.catalog().index);
    if (!child_bytes.succeeded() || !workspace.asset_pairs().publish(child_path.value(),
        child_bytes.value(), FilePublishMode::CreateNew).succeeded() || !workspace.refresh()) return 1;
    ThreadManager threads;
    auto graph_result = create_task_graph({0u, 256u, false}, threads);
    if (!graph_result.succeeded()) return 1;
    auto graph = graph_result.take_task_graph();
    if (!graph->attach_to_thread(NamedThread::GameThread).succeeded()) return 1;
    RenderingThread rendering(threads, *graph, RenderingThreadMode::SingleThread);
    if (!rendering.start().succeeded()) return 1;
    ShaderMapEntryLoader loader(PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
    ShaderMap map(loader);
    ShaderMapProgramKey key;
    key.shader_name = root.shader_name; key.pass_name = "Forward";
    const auto program = map.find_or_load(key);
    if (!program.succeeded()) { std::cerr << program.error; return 1; }
    TextureDesc white;
    white.width = 1u; white.height = 1u; white.format = PixelFormat::R8G8B8A8UNorm;
    white.row_pitches = {4u}; white.slice_pitches = {4u}; white.mip_pixels = {{255u, 255u, 255u, 255u}};
    TextureRef texture = Texture::create(std::move(white));
    MaterialInstanceRef defaults;
    {
        MaterialTextureValues textures;
        textures.named_defaults.emplace("white", texture);
        const auto built = create_material_from_asset(root, program.program, textures);
        if (!built.succeeded()) { std::cerr << built.status().message; return 1; }
        defaults = built.value();
    }
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(1024, 768); io.DeltaTime = 1.0f / 60.0f;
    unsigned char* font_pixels = nullptr;
    int font_width = 0; int font_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    check(font_pixels && font_width > 0 && font_height > 0, "ImGui font atlas");
    MaterialEditorPanel panel;
    // Engine teardown also runs when initialization failed before a session existed.
    panel.shutdown();
    panel.initialize(workspace, defaults->material(), PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
    EditorPanelRegistry panels;
    check(panels.add({"material", "Material Editor", "Material Editor", [&]() { panel.draw(); },
        [&]() { panel.undo(); }, [&]() { panel.redo(); }, [&]() { return panel.focused(); },
        [&]() { panel.save(); }}), "Material panel registers command callbacks");
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
            if (const auto* number = std::get_if<float>(&value.value)) inherited = *number == 64.0f;
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
        check(alternate_bytes.succeeded() && workspace.asset_pairs().publish(alternate_path.value(), alternate_bytes.value(),
            FilePublishMode::CreateNew).succeeded() && workspace.refresh(), "publish alternate Parent");
        MaterialInstanceAssetData grand;
        grand.parent = {child_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
        const auto grand_path = VirtualPath::parse("/Project/MI_UiGrand.asset");
        const auto grand_bytes = encode_material_instance_asset_pair(workspace.types(), grand_id, grand, &workspace.catalog().index);
        check(grand_bytes.succeeded() && workspace.asset_pairs().publish(grand_path.value(), grand_bytes.value(),
            FilePublishMode::CreateNew).succeeded() && workspace.refresh(), "publish descendant for cycle validation");
        const AssetRef next{alternate_id, {}, "toy3d.MaterialAssetData", AssetRefStrength::Strong};
        check(session.set_parent(next).succeeded() && session.instance_data()->parent.asset_id == alternate_id && session.dirty(),
            "Parent switch uses full preview candidate and records one asset edit");
        check(session.parameter_source("specular_power").asset_id == alternate_id && session.overrides().empty(),
            "source identifies the effective Parent without copying values");
        check(session.undo().succeeded() && session.instance_data()->parent.asset_id == root_id && !session.dirty(),
            "Parent undo restores original hierarchy and clean checkpoint");
        check(session.redo().succeeded() && session.instance_data()->parent.asset_id == alternate_id && session.undo().succeeded(),
            "Parent redo rebuilds the validated candidate");
        const auto undo_count = session.undo_count();
        const AssetRef descendant{grand_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
        check(!session.set_parent(descendant).succeeded() && session.undo_count() == undo_count && !session.dirty() &&
            session.instance_data()->parent.asset_id == root_id, "indirect Parent cycle preserves preview, history and file");
        const AssetRef self{child_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
        check(!session.set_parent(self).succeeded(), "self Parent is rejected");
    }
    check(session.set_parameter({"specular_power", 100.0f}).succeeded(), "child draft");
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
    session.set_publish([&publications](const AssetRef&) -> AssetStatus
    {
        ++publications;
        if (publications == 1) return {AssetErrorCode::InvalidState, {}, {}, {}, {}, "candidate publication failed", {}};
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
        read_material_instance_asset(workspace.types(), workspace.files(), child_path.value(), saved_child).succeeded() &&
        saved_child.overrides.size() == 1u, "saved file remains committed when rendering publication fails");
    check(session.publish_saved().succeeded() && publications == 2 && !session.dirty(), "Retry Publish does not rewrite or dirty the saved asset");
    check(panel.resolve_unsaved(MaterialCloseDecision::Cancel) && !panel.take_exit(), "cancel pending close after publication retry");
    dismiss_popup();
    session.set_publish({});
    panels.clear();
    panel.shutdown();
    panel.shutdown();
    ImGui::DestroyContext();
    MaterialInstance::release(defaults);
    check(texture.use_count() == 1u, "window closes release every shared default texture reference");
    Texture::release(texture);
    check(rendering.stop().succeeded(), "facade shutdown");
    check(graph->shutdown(TaskGraphShutdownMode::Drain).succeeded(), "graph shutdown");
    std::cout << (failures ? "Material UI tests failed\n" : "Material UI and close decisions passed\n");
    return failures ? 1 : 0;
}
