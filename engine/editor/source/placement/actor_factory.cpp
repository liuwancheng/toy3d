#include "placement/actor_factory.h"

#include <utility>

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

namespace toy3d
{
    namespace
    {
        StaticMeshRef make_builtin_cube()
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

            MaterialDesc material_desc;
            material_desc.shader_name = key.shader_name;
            material_desc.shader_program = std::move(loaded.program);
            material_desc.parameter_schema = material_parameter_schema_from_shader_schema(
                material_desc.shader_program->data().parameter_schema);
            material_desc.vector4_defaults.emplace(
                shader::make_shader_parameter_id(shader::BindingGroup::Material,
                                                  shader::ShaderParameterCategory::Constant, "base_color"),
                vec4(0.85f, 0.32f, 0.18f, 1.0f));
            const auto constant_id = [](const char* name)
            {
                return shader::make_shader_parameter_id(shader::BindingGroup::Material,
                                                        shader::ShaderParameterCategory::Constant, name);
            };
            material_desc.vector4_defaults.emplace(constant_id("ambient_color"),
                                                   vec4(0.08f, 0.10f, 0.14f, 1.0f));
            material_desc.vector4_defaults.emplace(constant_id("specular_color"),
                                                   vec4(1.0f, 0.92f, 0.78f, 1.0f));
            material_desc.scalar_defaults.emplace(constant_id("specular_power"), 32.0f);
            material_desc.scalar_defaults.emplace(constant_id("specular_intensity"), 0.35f);
            material_desc.texture_defaults.emplace(
                shader::make_shader_parameter_id(shader::BindingGroup::Material,
                                                  shader::ShaderParameterCategory::SampledTexture,
                                                  "surface_tint_texture"),
                std::move(white_texture));
            material_desc.two_sided = true;
            MaterialInstanceRef material = MaterialInstance::create(Material::create(std::move(material_desc)));
            if (!material)
                return nullptr;

            constexpr float h = 0.75f;
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
            mesh_desc.material_slots.push_back(std::move(material));
            return StaticMesh::create(std::move(mesh_desc));
        }
        StaticMeshRef instantiate_geometry(const StaticMeshRef& prototype)
        {
            if (!prototype) return nullptr;
            StaticMeshDesc desc;
            desc.vertices = prototype->vertices();
            desc.indices = prototype->indices();
            desc.sections = prototype->sections();
            desc.material_slots = prototype->material_slots();
            // Each placement has a fresh render-resource lifecycle. Released vertex
            // buffers discard their upload payload and cannot be reused on a redo.
            return StaticMesh::create(std::move(desc));
        }
    } // namespace


    EditorActorState capture_actor_state(const Actor& actor)
    {
        EditorActorState state;
        if (actor.root_component()) state.transform = actor.root_component()->local_transform();
        const auto* camera = dynamic_cast<const CameraComponent*>(actor.root_component());
        if (camera)
        {
            state.camera_vertical_fov = camera->vertical_fov_degrees();
            state.camera_near_clip = camera->near_clip();
            state.camera_far_clip = camera->far_clip();
        }
        const auto* light = dynamic_cast<const LightComponent*>(actor.root_component());
        if (light)
        {
            state.light_enabled = light->enabled();
            state.light_color = light->color();
            state.light_intensity = light->intensity();
            const auto* local = dynamic_cast<const LocalLightComponent*>(light);
            if (local) state.light_range = local->range();
        }
        return state;
    }

    bool apply_actor_state(Actor& actor, const EditorActorState& state)
    {
        if (!is_finite(state.light_color) || state.light_color.x < 0 || state.light_color.y < 0 ||
            state.light_color.z < 0 || !is_finite(state.light_intensity) || state.light_intensity < 0 ||
            !is_finite(state.light_range) || state.light_range <= 0) return false;
        SceneComponent* root = actor.root_component();
        if (!root) return false;
        auto* camera = dynamic_cast<CameraComponent*>(root);
        // Validate the complete camera edit before writing its Transform. A rejected
        // projection must not partially apply a history record.
        if (camera && !CameraComponent::is_valid_perspective(state.camera_vertical_fov,
                                                            state.camera_near_clip, state.camera_far_clip))
        {
            TOY_LOG_ERROR("Camera edit rejected an invalid or unrepresentable perspective projection.");
            return false;
        }
        const Transform& current = root->local_transform();
        if ((current.translation != state.transform.translation || current.rotation != state.transform.rotation ||
             current.scale != state.transform.scale) && !root->set_local_transform(state.transform)) return false;
        if (camera && (camera->vertical_fov_degrees() != state.camera_vertical_fov ||
            camera->near_clip() != state.camera_near_clip || camera->far_clip() != state.camera_far_clip) &&
            !camera->set_perspective(state.camera_vertical_fov, state.camera_near_clip, state.camera_far_clip))
            return false;
        auto* light = dynamic_cast<LightComponent*>(root);
        if (light)
        {
            if (light->enabled() != state.light_enabled) light->set_enabled(state.light_enabled);
            if (light->color() != state.light_color && !light->set_color(state.light_color)) return false;
            if (light->intensity() != state.light_intensity && !light->set_intensity(state.light_intensity)) return false;
            auto* local = dynamic_cast<LocalLightComponent*>(light);
            if (local && local->range() != state.light_range && !local->set_range(state.light_range)) return false;
        }
        return true;
    }

    // --------------------------------------------------------------------------
    // ActorFactory: owns builtin geometry and describes editor-created Actors
    // --------------------------------------------------------------------------
    bool ActorFactory::initialize()
    {
        cube_ = make_builtin_cube();
        if (!cube_) return false;
        material_ = cube_->material_slots().front();
        StaticMeshDesc plane;
        plane.vertices = {
            {{-2.5f, 0, -2.5f}, {0, 1, 0}, {0, 0}},
            {{ 2.5f, 0, -2.5f}, {0, 1, 0}, {1, 0}},
            {{ 2.5f, 0,  2.5f}, {0, 1, 0}, {1, 1}},
            {{-2.5f, 0,  2.5f}, {0, 1, 0}, {0, 1}}};
        // A fixed UInt16 alternative matches the small builtin geometry.
        plane.indices = std::vector<std::uint16_t>{0, 2, 1, 0, 3, 2};
        plane.sections.push_back({0, 6, 0});
        plane.material_slots.push_back(material_);
        plane_ = StaticMesh::create(std::move(plane));
        return plane_ != nullptr;
    }

    void ActorFactory::release()
    {
        placed_items_.clear();
        cube_.reset();
        plane_.reset();
        MaterialInstance::release(material_);
    }

    Actor* ActorFactory::create(World& world, const PlacementRequest& request)
    {
        if (!find_placement_item(request.item) || (!is_finite(request.transform.translation) || !is_finite(request.transform.rotation) || !is_finite(request.transform.scale)) ||
            request.transform.scale.x <= 0 || request.transform.scale.y <= 0 || request.transform.scale.z <= 0)
            return nullptr;
        if ((request.item == PlacementItemId::Cube && !cube_) ||
            (request.item == PlacementItemId::Plane && !plane_))
            return nullptr;
        StaticMeshRef geometry;
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
        placed_items_[actor->actor_id()] = request.item;
        return actor;
    }

    bool ActorFactory::describe(const Actor& actor, PlacementRequest& request) const
    {
        const auto found = placed_items_.find(actor.actor_id());
        if (found == placed_items_.end() || !actor.root_component()) return false;
        request.item = found->second;
        request.transform = actor.root_component()->local_transform();
        return true;
    }

    const char* ActorFactory::label(std::uint32_t actor_id) const
    {
        const auto found = placed_items_.find(actor_id);
        const PlacementItem* item = found == placed_items_.end() ? nullptr : find_placement_item(found->second);
        return item ? item->name : "Actor";
    }

    void ActorFactory::forget(std::uint32_t actor_id) { placed_items_.erase(actor_id); }
} // namespace toy3d
