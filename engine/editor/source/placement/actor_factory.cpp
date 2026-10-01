#include "placement/actor_factory.h"

#include <utility>
#include <algorithm>

#include "file_system/physical_path.h"
#include "format/shader_binding_identity.h"
#include "format/shader_format_types.h"
#include "gamescene/actor/camera_actor.h"
#include "gamescene/actor/light_actor.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/material/material_asset_builder.h"

namespace toy3d
{
    namespace
    {
        StaticMeshRef make_builtin_cube(MaterialInstanceRef& owner)
        {
            ShaderMapEntryLoader loader(PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
            ShaderMap shader_map(loader);
            ShaderMapProgramKey key;
            key.shader_name = "Toy3d/Surface/Phong";
            key.pass_name = "Forward";
            key.platform = ShaderPlatform::VulkanES31;
            ShaderMapProgramResult loaded = shader_map.find_or_load(key);
            if (!loaded.succeeded())
            {
                TOY_LOG_ERROR("Editor preview cube shader load failed: {}", loaded.error);
                return nullptr;
            }

            TextureDesc white_desc;
            white_desc.width = 1u;
            white_desc.height = 1u;
            white_desc.format = PixelFormat::R8G8B8A8UNorm;
            white_desc.row_pitches = {4u};
            white_desc.slice_pitches = {4u};
            white_desc.mip_pixels = {{255u, 255u, 255u, 255u}};
            TextureRef white_texture = Texture::create(std::move(white_desc));
            if (!white_texture)
                return nullptr;

            MaterialAssetData material_data;
            material_data.shader_name = key.shader_name;
            material_data.two_sided = true;
            MaterialTextureValues textures;
            textures.named_defaults.emplace("white", std::move(white_texture));
            auto built = create_material_from_asset(material_data, std::move(loaded.program), textures);
            if (!built.succeeded())
            {
                TOY_LOG_ERROR("Editor preview cube material build failed: {}", built.status().message);
                return nullptr;
            }
            MaterialInstanceRef material = built.value();
            if (!material)
                return nullptr;

            constexpr float h = meters_to_centimeters(0.75f);
            StaticMeshDesc mesh_desc;
            // Each face has its own normal; sharing corner vertices would smooth
            // the primitive and conceal the direction of editor lights.
            mesh_desc.vertices = {
                {{-h,-h,-h}, {0,0,-1}, {0,0}}, {{h,-h,-h}, {0,0,-1}, {1,0}},
                {{h,h,-h}, {0,0,-1}, {1,1}}, {{-h,h,-h}, {0,0,-1}, {0,1}},
                {{h,-h,h}, {0,0,1}, {0,0}}, {{-h,-h,h}, {0,0,1}, {1,0}},
                {{-h,h,h}, {0,0,1}, {1,1}}, {{h,h,h}, {0,0,1}, {0,1}},
                {{-h,-h,h}, {-1,0,0}, {0,0}}, {{-h,-h,-h}, {-1,0,0}, {1,0}},
                {{-h,h,-h}, {-1,0,0}, {1,1}}, {{-h,h,h}, {-1,0,0}, {0,1}},
                {{h,-h,-h}, {1,0,0}, {0,0}}, {{h,-h,h}, {1,0,0}, {1,0}},
                {{h,h,h}, {1,0,0}, {1,1}}, {{h,h,-h}, {1,0,0}, {0,1}},
                {{-h,h,-h}, {0,1,0}, {0,0}}, {{h,h,-h}, {0,1,0}, {1,0}},
                {{h,h,h}, {0,1,0}, {1,1}}, {{-h,h,h}, {0,1,0}, {0,1}},
                {{-h,-h,h}, {0,-1,0}, {0,0}}, {{h,-h,h}, {0,-1,0}, {1,0}},
                {{h,-h,-h}, {0,-1,0}, {1,1}}, {{-h,-h,-h}, {0,-1,0}, {0,1}}};
            // A fixed UInt16 alternative matches the small builtin geometry.
            mesh_desc.indices = std::vector<std::uint16_t>{
                0,1,2,0,2,3,4,5,6,4,6,7,8,9,10,8,10,11,
                12,13,14,12,14,15,16,17,18,16,18,19,20,21,22,20,22,23};
            mesh_desc.sections.push_back({0u, 36u, 0u});
            mesh_desc.material_slots.push_back(material);
            auto mesh = StaticMesh::create(std::move(mesh_desc));
            if (!mesh) return nullptr;
            owner = std::move(material);
            return mesh;
        }
        StaticMeshRef instantiate_geometry(const StaticMeshRef& prototype)
        {
            if (!prototype) return nullptr;
            StaticMeshDesc desc;
            desc.vertices = prototype->vertices();
            desc.vertex_colors = prototype->vertex_colors();
            desc.indices = prototype->indices();
            desc.sections = prototype->sections();
            desc.material_slots = prototype->material_slots();
            desc.material_slot_names = prototype->material_slot_names();
            // Each placement has a fresh render-resource lifecycle. Released vertex
            // buffers discard their upload payload and cannot be reused on a redo.
            return StaticMesh::create(std::move(desc));
        }
    } // namespace


    // --------------------------------------------------------------------------
    // ActorFactory: owns builtin geometry and describes editor-created Actors
    // --------------------------------------------------------------------------
    bool ActorFactory::initialize()
    {
        if (!component_editors_.freeze()) return false;
        cube_ = make_builtin_cube(material_);
        if (!cube_) return false;
        StaticMeshDesc plane;
        constexpr float k_plane_half_extent_cm = meters_to_centimeters(2.5f);
        plane.vertices = {
            {{-k_plane_half_extent_cm, 0, -k_plane_half_extent_cm}, {0, 1, 0}, {0, 0}},
            {{ k_plane_half_extent_cm, 0, -k_plane_half_extent_cm}, {0, 1, 0}, {1, 0}},
            {{ k_plane_half_extent_cm, 0,  k_plane_half_extent_cm}, {0, 1, 0}, {1, 1}},
            {{-k_plane_half_extent_cm, 0,  k_plane_half_extent_cm}, {0, 1, 0}, {0, 1}}};
        // A fixed UInt16 alternative matches the small builtin geometry.
        plane.indices = std::vector<std::uint16_t>{0, 2, 1, 0, 3, 2};
        plane.sections.push_back({0, 6, 0});
        plane.material_slots.push_back(material_);
        plane_ = StaticMesh::create(std::move(plane));
        return plane_ != nullptr;
    }

    StaticMeshRef ActorFactory::instantiate_builtin(const std::string& kind) const
    {
        if (kind == "Cube") return instantiate_geometry(cube_);
        if (kind == "Plane") return instantiate_geometry(plane_);
        return {};
    }

    void ActorFactory::remember(const Actor& actor, const PlacementRequest& request)
    {
        placed_items_[actor.actor_id()] = request;
    }

    EditorActorState ActorFactory::capture(const Actor& actor) const
    {
        EditorActorState state = capture_actor_state(actor, component_editors_);
        for (auto& snapshot : state.components)
        {
            // C++17 get_if augments mesh snapshots with their author identity.
            if (auto* mesh = std::get_if<SceneMeshData>(&snapshot.data.properties))
            {
                const auto* component = dynamic_cast<const SceneComponent*>(actor.find_component_by_id(snapshot.component_id));
                SceneMeshData source;
                if (component && mesh_source(*component, source))
                {
                    source.settings = mesh->settings;
                    *mesh = std::move(source);
                }
            }
        }
        return state;
    }

    bool ActorFactory::mesh_source(const SceneComponent& component, SceneMeshData& data) const
    {
        const auto found = mesh_sources_.find(component.component_id());
        const auto* mesh = dynamic_cast<const StaticMeshComponent*>(&component);
        if (!mesh || found == mesh_sources_.end() || found->second.geometry != mesh->static_mesh()) return false;
        data = found->second.data;
        data.settings = mesh->primitive_settings();
        return true;
    }

    void ActorFactory::remember_mesh(const SceneComponent& component, const SceneMeshData& data)
    {
        const auto* mesh = dynamic_cast<const StaticMeshComponent*>(&component);
        if (!mesh) return;
        SceneMeshData source = data;
        // Material asset identities belong to MaterialAssignments, not geometry provenance.
        source.resources.erase(std::remove_if(source.resources.begin(), source.resources.end(),
            [](const SceneResourceBinding& binding) { return binding.role != "mesh"; }), source.resources.end());
        mesh_sources_[component.component_id()] = {component.owner().actor_id(), mesh->static_mesh(), std::move(source)};
    }

    void ActorFactory::release()
    {
        placed_items_.clear();
        mesh_sources_.clear();
        cube_.reset();
        plane_.reset();
        MaterialInstance::release(material_);
    }

    Actor* ActorFactory::create(World& world, const PlacementRequest& request)
    {
        if ((!find_placement_item(request.item) && request.item != PlacementItemId::StaticMesh) || (!is_finite(request.transform.translation) || !is_finite(request.transform.rotation) || !is_finite(request.transform.scale)) ||
            request.transform.scale.x <= 0 || request.transform.scale.y <= 0 || request.transform.scale.z <= 0)
            return nullptr;
        if ((request.item == PlacementItemId::Cube && !cube_) ||
            (request.item == PlacementItemId::Plane && !plane_))
            return nullptr;
        StaticMeshRef geometry;
        if (request.item == PlacementItemId::StaticMesh)
        {
            if (!request.asset_id.valid()) return nullptr;
            geometry = instantiate_geometry(request.static_mesh);
            if (!geometry) return nullptr;
        }
        if (request.item == PlacementItemId::Cube || request.item == PlacementItemId::Plane)
        {
            geometry = instantiate_geometry(request.item == PlacementItemId::Cube ? cube_ : plane_);
            if (!geometry) return nullptr;
        }
        Actor* actor = nullptr;
        switch (request.item)
        {
        case PlacementItemId::EmptyActor:
            actor = &world.spawn_actor<Actor>();
            if (!actor->set_root_component(&actor->create_component<SceneComponent>()))
            {
                if (!world.destroy_actor(*actor)) TOY_LOG_ERROR("Placement rollback failed.");
                return nullptr;
            }
            break;
        case PlacementItemId::Cube:
        case PlacementItemId::Plane:
        case PlacementItemId::StaticMesh:
            actor = &world.spawn_actor<StaticMeshActor>();
            break;
        case PlacementItemId::DirectionalLight:
            actor = &world.spawn_actor<DirectionalLightActor>();
            break;
        case PlacementItemId::PointLight:
            actor = &world.spawn_actor<PointLightActor>();
            break;
        case PlacementItemId::Camera:
            actor = &world.spawn_actor<CameraActor>();
            break;
        }
        if (!actor || !actor->root_component()->set_local_transform(request.transform))
        {
            if (actor && !world.destroy_actor(*actor)) TOY_LOG_ERROR("Placement rollback failed.");
            return nullptr;
        }
        if (auto* mesh_actor = dynamic_cast<StaticMeshActor*>(actor))
            mesh_actor->static_mesh_component().set_static_mesh(std::move(geometry));
        placed_items_[actor->actor_id()] = request;
        if (auto* mesh = dynamic_cast<StaticMeshComponent*>(actor->root_component()))
        {
            SceneMeshData source;
            if (request.item == PlacementItemId::Cube) source.builtin_mesh = "Cube";
            else if (request.item == PlacementItemId::Plane) source.builtin_mesh = "Plane";
            else source.resources.push_back({"mesh", {request.asset_id, {}, "toy3d.StaticMeshAssetData", AssetRefStrength::Strong}});
            remember_mesh(*mesh, source);
        }
        return actor;
    }

    Actor* ActorFactory::restore(World& world, const PlacementRequest& request, const EditorActorState& state,
                                 std::map<std::uint32_t, std::uint32_t>& component_ids)
    {
        if (!state.valid || state.components.empty()) return nullptr;
        for (const auto& snapshot : state.components)
            if (!component_editors_.find(snapshot.data.type) || !validate_component_data(snapshot.data)) return nullptr;
        Actor* actor = request.item == PlacementItemId::EmptyActor ? &world.spawn_actor<Actor>() : create(world, request);
        if (!actor) return nullptr;
        EditorActorState restored = state;
        std::vector<std::uint32_t> unused = actor->component_ids();
        bool valid = true;
        for (auto& snapshot : restored.components)
        {
            SceneComponent* component = nullptr;
            for (auto entry = unused.begin(); entry != unused.end(); ++entry)
            {
                auto* candidate = dynamic_cast<SceneComponent*>(actor->find_component_by_id(*entry));
                const auto* editor = candidate ? component_editors_.find(*candidate) : nullptr;
                if (editor && editor->persistent_type == snapshot.data.type)
                {
                    component = candidate;
                    unused.erase(entry);
                    break;
                }
            }
            if (!component) component = &component_editors_.find(snapshot.data.type)->create(*actor);
            component_ids.emplace(snapshot.component_id, component->component_id());
            snapshot.component_id = component->component_id();
            if (auto* mesh = dynamic_cast<StaticMeshComponent*>(component))
            {
                if (!snapshot.mesh) { valid = false; break; }
                mesh->set_static_mesh(instantiate_geometry(snapshot.mesh));
                if (!mesh->static_mesh()) { valid = false; break; }
                // C++17 get_if restores the geometry source alongside the new Component ID.
                if (const auto* data = std::get_if<SceneMeshData>(&snapshot.data.properties)) remember_mesh(*mesh, *data);
            }
        }
        if (!unused.empty()) valid = false;
        const auto root = component_ids.find(state.root_component_id);
        if (root == component_ids.end()) valid = false;
        if (valid)
        {
            restored.root_component_id = root->second;
            for (auto& snapshot : restored.components)
            {
                const auto parent = component_ids.find(snapshot.parent_component_id);
                if (parent != component_ids.end())
                {
                    snapshot.parent_actor_id = actor->actor_id();
                    snapshot.parent_component_id = parent->second;
                }
            }
            valid = actor->set_root_component(static_cast<SceneComponent*>(actor->find_component_by_id(root->second))) &&
                    apply_actor_state(*actor, restored, component_editors_) && restore_actor_attachments(*actor, restored);
        }
        if (valid) { remember(*actor, request); return actor; }
        const auto id = actor->actor_id();
        if (!world.destroy_actor(*actor)) TOY_LOG_ERROR("Component reconstruction rollback failed.");
        forget(id);
        component_ids.clear();
        return nullptr;
    }

    bool ActorFactory::describe(const Actor& actor, PlacementRequest& request) const
    {
        const auto found = placed_items_.find(actor.actor_id());
        if (!actor.root_component()) return false;
        if (found != placed_items_.end()) request = found->second;
        else if (typeid(actor) == typeid(Actor)) request.item = PlacementItemId::EmptyActor;
        else if (typeid(actor) == typeid(DirectionalLightActor)) request.item = PlacementItemId::DirectionalLight;
        else if (typeid(actor) == typeid(PointLightActor)) request.item = PlacementItemId::PointLight;
        else if (typeid(actor) == typeid(CameraActor)) request.item = PlacementItemId::Camera;
        else return false;
        request.transform = actor.root_component()->local_transform();
        return true;
    }

    const char* ActorFactory::label(std::uint32_t actor_id) const
    {
        const auto found = placed_items_.find(actor_id);
        if (found != placed_items_.end() && found->second.item == PlacementItemId::StaticMesh) return "Static Mesh";
        const PlacementItem* item = found == placed_items_.end() ? nullptr : find_placement_item(found->second.item);
        return item ? item->name : "Actor";
    }

    void ActorFactory::forget(std::uint32_t actor_id)
    {
        placed_items_.erase(actor_id);
        for (auto item = mesh_sources_.begin(); item != mesh_sources_.end();)
            if (item->second.actor_id == actor_id) item = mesh_sources_.erase(item);
            else ++item;
    }
} // namespace toy3d
