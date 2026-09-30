#include "material/material_assignments.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"

#include "commands/editor_command_history.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/world/world.h"
#include "panels/scene_panels.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/render_command.h"
#include "rendercore/rendering_thread.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "rendercore/scene_interface.h"
#include "selection/editor_selection.h"
#include "task_graph/task_graph.h"
#include "threading/thread_manager.h"
#include "viewport/scene_viewport.h"
#include "workspace/editor_workspace.h"

namespace
{
    int failures = 0;
    void check(bool value, const char* message)
    {
        if (!value) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
    }

    // Actual owned FIFO commands exercise pending Component Remove captures in
    // both single-thread and multi-thread mode without creating a GPU device.
    class TestScene final : public toy3d::SceneInterface
    {
      public:
        void add_primitive(std::unique_ptr<toy3d::PrimitiveSceneProxy> proxy) override
        {
            toy3d::enqueue_render_command("TestAddPrimitive", [this, proxy = std::move(proxy)]() mutable noexcept
            { auto* id = proxy.get(); proxies_.emplace(id, std::move(proxy)); ++count; });
        }
        void remove_primitive(toy3d::PrimitiveSceneProxy* proxy) override
        {
            toy3d::enqueue_render_command("TestRemovePrimitive", [this, proxy]() noexcept
            { if (proxies_.erase(proxy) == 1u) --count; else ++invalid_removes; });
        }
        void update_primitive_transform(toy3d::PrimitiveSceneProxy*, toy3d::Matrix4, toy3d::AxisAlignedBounds, bool) override {}
        void update_primitive_materials(toy3d::PrimitiveSceneProxy* proxy, std::vector<toy3d::MaterialRenderProxy*> materials) override
        {
            toy3d::enqueue_render_command("TestUpdateMaterials", [this, proxy, materials = std::move(materials)]() mutable noexcept
            {
                const auto found = proxies_.find(proxy);
                auto* mesh = found == proxies_.end() ? nullptr : dynamic_cast<toy3d::StaticMeshSceneProxy*>(found->second.get());
                if (!mesh) { ++invalid_removes; return; }
                mesh->set_material_render_proxies(std::move(materials));
            });
        }
        void add_light(std::unique_ptr<toy3d::LightSceneProxy>) override {}
        void update_light(toy3d::LightSceneProxy*, toy3d::LightSceneData) override {}
        void remove_light(toy3d::LightSceneProxy*) override {}
        std::atomic<int> count{0};
        std::atomic<int> invalid_removes{0};
      private:
        std::map<toy3d::PrimitiveSceneProxy*, std::unique_ptr<toy3d::PrimitiveSceneProxy>> proxies_;
    };
}

int main(int argc, char** argv)
{
    using namespace toy3d;
    const bool multithreaded = argc > 1 && std::string(argv[1]) == "--multithread";
    NativePlatformFile platform;
    AssetId red_id; AssetId blue_id; AssetId child_id; AssetId mesh_id; AssetId missing_id;
    if (!AssetId::try_generate(red_id) || !AssetId::try_generate(blue_id) || !AssetId::try_generate(child_id) ||
        !AssetId::try_generate(mesh_id) || !AssetId::try_generate(missing_id)) return 1;
    EditorWorkspacePaths paths;
    paths.project_assets = PhysicalPath(std::string(TOY3D_MATERIAL_TEST_ROOT) + "/" + red_id.hex());
    paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
    paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
    if (!platform.create_directories(paths.project_assets).succeeded()) return 1;
    EditorWorkspace workspace;
    if (!workspace.initialize(paths)) { std::cerr << workspace.error(); return 1; }
    MaterialAssetData red; red.shader_name = "Toy3d/Surface/Phong";
    red.overrides.push_back({"base_color", Vector4(1, 0, 0, 1)});
    MaterialAssetData blue = red; blue.overrides[0].value = Vector4(0, 0, 1, 1); blue.two_sided = true;
    const auto red_path = VirtualPath::parse("/Project/M_Red.asset");
    const auto blue_path = VirtualPath::parse("/Project/M_Blue.asset");
    const auto child_path = VirtualPath::parse("/Project/MI_Blue.asset");
    if (!red_path.succeeded() || !blue_path.succeeded() || !child_path.succeeded()) return 1;
    auto write_root = [&](const AssetId& id, const MaterialAssetData& data, const VirtualPath& path, FilePublishMode mode)
    {
        const auto bytes = encode_material_asset(workspace.types(), id, data);
        return bytes.succeeded() && workspace.files().write_binary_atomic(path, bytes.value(), mode).succeeded();
    };
    if (!write_root(red_id, red, red_path.value(), FilePublishMode::CreateNew) ||
        !write_root(blue_id, blue, blue_path.value(), FilePublishMode::CreateNew) || !workspace.refresh()) return 1;
    MaterialInstanceAssetData child;
    child.parent.asset_id = blue_id; child.parent.expected_type = "toy3d.MaterialAssetData";
    child.overrides.push_back({"specular_power", 64.0f});
    const auto child_bytes = encode_material_instance_asset(workspace.types(), child_id, child, &workspace.catalog().index);
    if (!child_bytes.succeeded() || !workspace.files().write_binary_atomic(child_path.value(), child_bytes.value(),
        FilePublishMode::CreateNew).succeeded() || !workspace.refresh()) return 1;
    ThreadManager threads;
    auto graph_result = create_task_graph({multithreaded ? 1u : 0u, 256u, multithreaded}, threads);
    if (!graph_result.succeeded()) return 1;
    auto graph = graph_result.take_task_graph();
    if (!graph->attach_to_thread(NamedThread::GameThread).succeeded()) return 1;
    RenderingThread rendering(threads, *graph, multithreaded ? RenderingThreadMode::MultiThread : RenderingThreadMode::SingleThread);
    if (!rendering.start().succeeded()) return 1;
    ActorFactory factory;
    if (!factory.initialize()) return 1;
    MaterialAssignments materials;
    const auto defaults = factory.default_material()->material();
    MaterialTextureValues textures;
    for (const auto& resource : defaults->parameter_schema().resources)
        textures.named_defaults[resource.default_value] = defaults->desc().texture_defaults.at(resource.parameter_id);
    MaterialLibrary library(workspace.types(), workspace.files(),
        [&workspace]() -> const AssetIndex& { return workspace.catalog().index; },
        [defaults](const std::string& name) { return name == defaults->desc().shader_name ? defaults->desc().shader_program : nullptr; }, textures);
    materials.initialize(workspace, library);
    {
        AssetId grand_id;
        check(AssetId::try_generate(grand_id), "allocate grandchild identity");
        MaterialInstanceAssetData grand;
        grand.parent.asset_id = child_id;
        grand.parent.expected_type = "toy3d.MaterialInstanceAssetData";
        const auto grand_path = VirtualPath::parse("/Project/MI_Grand.asset");
        auto bytes = encode_material_instance_asset(workspace.types(), grand_id, grand, &workspace.catalog().index);
        check(bytes.succeeded() && workspace.files().write_binary_atomic(grand_path.value(), bytes.value(), FilePublishMode::CreateNew).succeeded()
            && workspace.refresh(), "publish empty third layer");
        AssetRef grand_ref{grand_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
        const auto grand_loaded = library.load(grand_ref);
        check(grand_loaded.succeeded(), "load three-layer shared graph");
        auto shared = grand_loaded.value();
        const auto repeated = library.load(grand_ref);
        check(repeated.succeeded() && repeated.value() == shared, "repeated load returns stable shared identity");
        auto first = library.create_instance(shared).value();
        auto second = library.create_instance(first).value();
        auto sibling = library.create_instance(shared).value();
        MaterialParameterValue value;
        const auto scalar = [&](const MaterialInterfaceRef& material)
        {
            if (!material->parameter_value("specular_power", value)) return -1.0f;
            // C++17 get_if checks the effective query's actual runtime shape.
            const auto* number = std::get_if<float>(&value);
            return number ? *number : -1.0f;
        };
        check(scalar(second) == 64.0f && !second->overrides_parameter("specular_power"), "temporary chain inherits without local copies");
        check(first->set_scalar("specular_power", 96.0f) && scalar(second) == 96.0f && scalar(sibling) == 64.0f,
            "temporary parent updates descendants while keeping siblings isolated");
        check(second->set_scalar("specular_power", 100.0f) && first->set_scalar("specular_power", 120.0f) && scalar(second) == 100.0f,
            "local override wins after parent changes");
        check(second->reset_parameter("specular_power") && scalar(second) == 120.0f && first->reset_parameter("specular_power"),
            "reset reads latest direct Parent value");
        auto changed = child;
        changed.overrides = {{"specular_power", 80.0f}};
        bytes = encode_material_instance_asset(workspace.types(), child_id, changed, &workspace.catalog().index);
        check(bytes.succeeded() && workspace.files().write_binary_atomic(child_path.value(), bytes.value(), FilePublishMode::Replace).succeeded()
            && workspace.refresh(), "save changed middle layer");
        AssetRef child_reference{child_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
        auto* const proxy = shared->material_render_proxy();
        check(library.reload(child_reference).succeeded() && scalar(shared) == 80.0f && scalar(second) == 80.0f && scalar(sibling) == 80.0f
            && shared->material_render_proxy() == proxy, "saved middle layer publishes through asset and temporary descendants with stable Proxy");
        MaterialInstanceAssetData retained;
        check(read_material_instance_asset(workspace.types(), workspace.files(), grand_path.value(), retained).succeeded() && retained.overrides.empty(),
            "parent publication does not rewrite child file or copy inherited values");
        auto saved_blue = blue;
        saved_blue.two_sided = false;
        check(write_root(blue_id, saved_blue, blue_path.value(), FilePublishMode::Replace) && workspace.refresh(),
            "save ancestor without publishing it");
        AssetId unpublished_child_id;
        check(AssetId::try_generate(unpublished_child_id), "allocate not-yet-loaded child");
        MaterialInstanceAssetData unpublished_child;
        unpublished_child.parent = child_reference;
        const auto unpublished_path = VirtualPath::parse("/Project/MI_UnpublishedParent.asset");
        const auto unpublished_bytes = encode_material_instance_asset(workspace.types(), unpublished_child_id,
            unpublished_child, &workspace.catalog().index);
        check(unpublished_bytes.succeeded() && workspace.files().write_binary_atomic(unpublished_path.value(),
            unpublished_bytes.value(), FilePublishMode::CreateNew).succeeded() && workspace.refresh(),
            "save new descendant of unpublished ancestor");
        const AssetRef unpublished_reference{unpublished_child_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
        check(!library.load(unpublished_reference).succeeded(),
            "new child load cannot mix saved ancestor data with an old active Parent");
        const auto rejected = library.reload(child_reference);
        check(!rejected.succeeded() && rejected.message.find("Reload the Parent first") != std::string::npos &&
            shared->desc().two_sided && scalar(second) == 80.0f && shared->material_render_proxy() == proxy,
            "child publication rejects unpublished ancestor and preserves active graph");
        const AssetRef blue_reference{blue_id, {}, "toy3d.MaterialAssetData", AssetRefStrength::Strong};
        check(library.reload(blue_reference).succeeded() && !shared->desc().two_sided &&
            !second->desc().two_sided && library.reload(child_reference).succeeded() &&
            library.load(unpublished_reference).succeeded(),
            "publishing ancestor makes child retry valid and updates temporary descendants");
        check(write_root(blue_id, blue, blue_path.value(), FilePublishMode::Replace) && workspace.refresh() &&
            library.reload(blue_reference).succeeded(), "restore published ancestor fixture");
        auto deepest = library.create_instance(shared).value();
        for (std::size_t depth = 4u; depth < maximum_material_parent_depth; ++depth)
            deepest = library.create_instance(deepest).value();
        check(!library.create_instance(deepest).succeeded(), "temporary chain also enforces the common 64-layer limit");
        grand.parent = unpublished_reference;
        bytes = encode_material_instance_asset(workspace.types(), grand_id, grand, &workspace.catalog().index);
        check(bytes.succeeded() && workspace.files().write_binary_atomic(grand_path.value(), bytes.value(), FilePublishMode::Replace).succeeded()
            && workspace.refresh() && !library.reload(grand_ref).succeeded() && shared->parent() != library.load(unpublished_reference).value(),
            "reparent rejects a prospective graph that would push temporary descendants beyond the depth limit");
        grand.parent = child_reference;
        bytes = encode_material_instance_asset(workspace.types(), grand_id, grand, &workspace.catalog().index);
        check(bytes.succeeded() && workspace.files().write_binary_atomic(grand_path.value(), bytes.value(), FilePublishMode::Replace).succeeded()
            && workspace.refresh() && library.reload(grand_ref).succeeded(), "restore Parent after rejected deep temporary graph");
        auto disposable = library.create_instance(shared).value();
        auto borrowed = disposable;
        check(!library.release_instance(disposable).succeeded() && disposable,
            "temporary release rejects a remaining user");
        borrowed.reset();
        check(library.release_instance(disposable).succeeded() && !disposable,
            "temporary release removes the library owner and caller reference");
        grand.parent.asset_id = red_id;
        grand.parent.expected_type = "toy3d.MaterialAssetData";
        bytes = encode_material_instance_asset(workspace.types(), grand_id, grand, &workspace.catalog().index);
        check(bytes.succeeded() && workspace.files().write_binary_atomic(grand_path.value(), bytes.value(), FilePublishMode::Replace).succeeded()
            && workspace.refresh() && library.reload(grand_ref).succeeded(), "reparent shared asset to another root");
        const auto red_loaded = library.load(grand.parent);
        check(shared->parent() == red_loaded.value() && &second->root_material() == &red_loaded.value()->root_material(),
            "reparent updates direct Parent and root query for temporary descendants");
        bytes = encode_material_instance_asset(workspace.types(), child_id, child, &workspace.catalog().index);
        check(bytes.succeeded() && workspace.files().write_binary_atomic(child_path.value(), bytes.value(), FilePublishMode::Replace).succeeded()
            && workspace.refresh() && library.reload(child_reference).succeeded(), "restore middle layer fixture");
    }
    EditorCommandHistory history(factory, materials);
    TestScene scene;
    World world;
    check(world.bind_scene(scene), "bind FIFO test scene");
    StaticMeshDesc desc;
    desc.vertices = {{{0,0,0}, {0,1,0}, {0,0}}, {{1,0,0}, {0,1,0}, {1,0}}, {{0,0,1}, {0,1,0}, {0,1}}};
    // C++17 variant selects one fixed index width for this two-section fixture.
    desc.indices = std::vector<std::uint16_t>{0,2,1,0,2,1};
    desc.sections = {{0,3,0}, {3,3,1}};
    desc.material_slots = {factory.default_material(), factory.default_material()};
    desc.material_slot_names = {"Body", "Trim"};
    PlacementRequest request;
    request.item = PlacementItemId::StaticMesh; request.asset_id = mesh_id;
    request.static_mesh = StaticMesh::create(desc);
    check(request.static_mesh != nullptr, "named multi-slot geometry");
    auto invalid_desc = desc; invalid_desc.material_slot_names = {"Body", "Body"};
    check(!StaticMesh::create(std::move(invalid_desc)), "duplicate slot names are rejected");
    invalid_desc = desc; invalid_desc.material_slot_names = {"Body"};
    check(!StaticMesh::create(std::move(invalid_desc)), "slot name count mismatch rejected");
    auto id = history.place_actor(world, request);
    auto* actor = dynamic_cast<StaticMeshActor*>(world.find_actor_by_id(id));
    if (!actor) return 1;
    auto* component = &actor->static_mesh_component();
    check(component->static_mesh()->material_slot_names() == desc.material_slot_names, "placement preserves stable names");
    AssetRef red_ref; red_ref.asset_id = red_id; red_ref.expected_type = "toy3d.MaterialAssetData";
    AssetRef blue_ref = red_ref; blue_ref.asset_id = blue_id;
    AssetRef child_ref; child_ref.asset_id = child_id; child_ref.expected_type = "toy3d.MaterialInstanceAssetData";
    std::string error;
    check(history.assign_material(world, id, component->component_id(), "Body", red_ref, error), "assign red Body");
    auto old_red = component->material_for_slot(0);
    check(history.assign_material(world, id, component->component_id(), "Trim", blue_ref, error), "assign blue Trim");
    check(component->material_for_slot(0) == old_red && component->material_for_slot(1) != old_red &&
        component->material_for_slot(1)->desc().two_sided, "slots remain independent and structural root state loads");
    check(history.undo(world) && !component->has_material_override(1), "undo second slot assignment");
    AssetRef bad = red_ref; bad.asset_id = missing_id;
    check(!history.assign_material(world, id, component->component_id(), "Trim", bad, error) && !error.empty(), "missing asset rejected");
    check(!history.assign_material(world, id, component->component_id(), "MissingSlot", blue_ref, error), "missing slot rejected");
    bad = red_ref; bad.expected_type = "toy3d.StaticMeshAssetData";
    check(!history.assign_material(world, id, component->component_id(), "Trim", bad, error), "wrong type rejected");
    check(history.assign_material(world, id, component->component_id(), "Body", red_ref, error), "same assignment is a no-op");
    check(history.redo(world) && component->has_material_override(1), "failures and no-op preserve redo");
    check(history.assign_material(world, id, component->component_id(), "Body", {}, error) &&
        component->material_for_slot(0) == factory.default_material(), "reset removes override and reveals mesh default");
    check(history.undo(world) && component->material_for_slot(0) == old_red, "undo reset resolves saved material");
    check(history.assign_material(world, id, component->component_id(), "Trim", child_ref, error) &&
        component->material_for_slot(1)->desc().two_sided, "single-layer instance inherits parent structure");
    const auto old_component_id = component->component_id();
    check(history.delete_actor(world, id) && world.actor_count() == 0, "delete captures material references");
    check(history.undo(world), "delete undo rebuilds named assignments");
    id = world.actor_ids().front(); actor = dynamic_cast<StaticMeshActor*>(world.find_actor_by_id(id));
    component = &actor->static_mesh_component();
    check(component->component_id() != old_component_id && component->material_for_slot(0) == old_red &&
        materials.reference(world, id, component->component_id(), "Trim").asset_id == child_id, "new Actor/Component identities restore both slots");
    check(history.undo(world) && materials.reference(world, id, component->component_id(), "Trim").asset_id == blue_id,
        "earlier material undo targets remapped component");
    check(history.redo(world) && history.redo(world) && world.actor_count() == 0 && history.undo(world), "redo assignment/delete and reconstruct again");
    id = world.actor_ids().front(); actor = dynamic_cast<StaticMeshActor*>(world.find_actor_by_id(id));
    component = &actor->static_mesh_component();
    check(history.undo(world), "material history remaps through repeated reconstruction");
    check(history.redo(world), "redo remapped material command");

    // Publication updates the shared identity in every existing slot.
    red.two_sided = true;
    check(write_root(red_id, red, red_path.value(), FilePublishMode::Replace), "publish changed saved material");
    check(history.assign_material(world, id, component->component_id(), "Trim", red_ref, error), "new assignment reads latest saved version");
    check(materials.reload(red_ref).succeeded(), "reload publishes saved values to all users");
    check(component->material_for_slot(1) == old_red && old_red->desc().two_sided,
        "shared material identity is stable after publication");
    auto stable = component->material_for_slot(1);
    MaterialAssetData unsupported = blue; unsupported.shader_name = "Project/Unknown";
    check(write_root(blue_id, unsupported, blue_path.value(), FilePublishMode::Replace), "publish unsupported shader fixture");
    check(!history.assign_material(world, id, component->component_id(), "Trim", blue_ref, error) &&
        component->material_for_slot(1) == stable, "bad shader leaves old effect and command history intact");
    check(write_root(blue_id, blue, blue_path.value(), FilePublishMode::Replace), "restore fixture shader");
    check(history.delete_actor(world, id), "delete before failed reconstruction");
    check(write_root(blue_id, unsupported, blue_path.value(), FilePublishMode::Replace), "invalidate material dependency");
    // Trim now references red; invalidate red as well to exercise full rollback.
    auto invalid_red = red; invalid_red.shader_name = "Project/Unknown";
    check(write_root(red_id, invalid_red, red_path.value(), FilePublishMode::Replace), "invalidate reconstruction root");
    check(!history.undo(world) && world.actor_count() == 0, "failed reconstruction removes candidate and preserves history");
    check(write_root(red_id, red, red_path.value(), FilePublishMode::Replace) &&
        write_root(blue_id, blue, blue_path.value(), FilePublishMode::Replace) && history.undo(world), "retry reconstruction after dependency repair");
    id = world.actor_ids().front(); actor = dynamic_cast<StaticMeshActor*>(world.find_actor_by_id(id));
    component = &actor->static_mesh_component();

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(1200, 700); io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr; int width = 0; int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    EditorSelection selection; selection.select_actor(world, id);
    SceneViewport viewport;
    auto details = [&]()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(600, 650));
        draw_details(world, selection, history, workspace, viewport, materials, error);
    };
    ImGui::NewFrame(); details(); ImGui::Render();
    check(ImGui::GetDrawData()->TotalVtxCount > 0, "actual Details panel produces material controls");
    selection.select_asset(blue_id);
    const ImGuiID slot_id = ImHashStr("Body", 0, ImGui::FindWindowByName("Details")->ID);
    const ImGuiID combo_id = ImHashStr("##Material", 0, slot_id);
    float target_y = 0;
    for (float y = 80; y < 350 && target_y == 0; y += 8)
    {
        io.AddMousePosEvent(120, y); io.AddMouseButtonEvent(0, true);
        ImGui::NewFrame();
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern))
        {
            ImGui::SetDragDropPayload(MATERIAL_ASSET_DRAG_PAYLOAD, &blue_id, sizeof(blue_id));
            ImGui::TextUnformatted("Material test payload"); ImGui::EndDragDropSource();
        }
        details();
        if (ImGui::GetCurrentContext()->DragDropAcceptIdCurr == combo_id) target_y = y;
        check(!(materials.reference(world, id, component->component_id(), "Body").asset_id == blue_id), "hover cannot assign a material");
        ImGui::Render();
    }
    check(target_y != 0, "asset-focus drag exposes the retained Actor's exact Body slot target");
    io.AddMousePosEvent(120, target_y); io.AddMouseButtonEvent(0, false);
    ImGui::NewFrame();
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern))
    {
        ImGui::SetDragDropPayload(MATERIAL_ASSET_DRAG_PAYLOAD, &blue_id, sizeof(blue_id));
        ImGui::EndDragDropSource();
    }
    details(); ImGui::Render();
    check(selection.focus() == EditorSelectionFocus::Actor &&
        materials.reference(world, id, component->component_id(), "Body").asset_id == blue_id, "actual drag delivery assigns and restores Actor focus");
    check(history.undo(world), "Details drag enters scene undo timeline");
    // A material payload on empty viewport space is never a mesh placement.
    io.AddMousePosEvent(900, 300);
    ImGui::NewFrame(); viewport.begin_frame();
    ImGui::SetNextWindowPos(ImVec2(600, 0)); ImGui::SetNextWindowSize(ImVec2(600, 650));
    viewport.draw(world, selection, history); ImGui::Render();
    for (int frame = 0; frame < 2; ++frame)
    {
        io.AddMouseButtonEvent(0, frame == 0); ImGui::NewFrame();
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern))
        { ImGui::SetDragDropPayload(MATERIAL_ASSET_DRAG_PAYLOAD, &blue_id, sizeof(blue_id)); ImGui::EndDragDropSource(); }
        viewport.begin_frame(); ImGui::SetNextWindowPos(ImVec2(600, 0)); ImGui::SetNextWindowSize(ImVec2(600, 650));
        viewport.draw(world, selection, history);
        AssetPlacementRequest placement;
        check(!viewport.take_asset_placement(placement) && world.actor_count() == 1, "background material drop does not mutate World");
        ImGui::Render();
    }
    ImGui::DestroyContext();

    // Name resolution follows reordering; a missing name cannot replay into
    // whichever slot happens to occupy the old numeric index.
    history.clear();
    check(materials.assign(world, id, {component->component_id(), "Body", {}}, error) &&
        materials.assign(world, id, {component->component_id(), "Trim", {}}, error), "clear fixture bindings");
    check(history.assign_material(world, id, component->component_id(), "Body", red_ref, error) && history.undo(world), "prepare name-based replay");
    auto reordered = desc; reordered.material_slot_names = {"Trim", "Body"};
    component->set_static_mesh(StaticMesh::create(reordered));
    check(history.redo(world) && !component->has_material_override(0) && component->has_material_override(1), "redo resolves slot by name after reordering");
    check(history.undo(world), "undo reordered named assignment");
    reordered.material_slot_names = {"Trim", "Replacement"};
    component->set_static_mesh(StaticMesh::create(reordered));
    check(!history.redo(world) && !component->has_material_override(0) && !component->has_material_override(1), "missing slot replay fails without index guessing");
    old_red.reset(); stable.reset(); request.static_mesh.reset(); desc.material_slots.clear(); reordered.material_slots.clear();
    invalid_desc.material_slots.clear();
    history.clear();
    for (const auto actor_id : world.actor_ids()) check(world.destroy_actor(*world.find_actor_by_id(actor_id)), "destroy all scene users");
    check(flush_rendering_commands().succeeded() && scene.count == 0 && scene.invalid_removes == 0, "FIFO removes drain before final Material release");
    check(world.unbind_scene(), "unbind scene");
    materials.shutdown(); library.shutdown(); factory.release();
    check(flush_rendering_commands().succeeded(), "drain final releases");
    check(rendering.stop().succeeded() && graph->shutdown(TaskGraphShutdownMode::Drain).succeeded(), "shutdown facade and TaskGraph");
    std::cout << (failures ? "Material assignment tests failed\n" : "Material assignment tests passed\n");
    return failures ? 1 : 0;
}
