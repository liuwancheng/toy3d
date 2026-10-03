#include "scene/editor_play_session.h"
#include "rendercore/texture/texture_asset_loader.h"

#include <algorithm>
#include <cmath>

#include "asset/mesh/static_mesh_asset.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/component/skeletal_mesh_component.h"
#include "rendercore/geometry/skeletal_mesh_asset_loader.h"
#include "logging/logger.h"
#include "input/input_system.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    EditorPlaySession::~EditorPlaySession()
    {
        stop();
    }

    bool EditorPlaySession::start(
        const SceneAssetData& data, EditorWorkspace& workspace, const ActorTypeRegistry& actors,
        const std::function<ShaderMapCollectionRef(const std::string&,
                                                   const std::vector<shader::ShaderPermutationSelection>&)>& programs,
        SceneInterface& scene)
    {
        if (active())
        {
            error_ = "A Play session is already active.";
            return false;
        }
        error_.clear();
        if (!InputSystem::get_instance().begin_play_session())
        {
            error_ = "Another gameplay input session is active.";
            return false;
        }
        input_session_owned_ = true;
        const auto program = programs("Toy3d/Surface/Phong", {});
        if (!program || !geometry_.initialize({}, program))
        {
            error_ = "Play requires a validated published Phong Program. Recompile Shaders first.";
            geometry_.release();
            InputSystem::get_instance().end_play_session();
            input_session_owned_ = false;
            return false;
        }
        const auto defaults = geometry_.default_material()->material();
        MaterialTextureValues textures;
        for (const auto& resource : defaults->parameter_schema().resources)
        {
            const auto found = defaults->desc().texture_defaults.find(resource.parameter_id);
            if (found != defaults->desc().texture_defaults.end())
            {
                textures.named_defaults[resource.default_value] = found->second;
            }
        }
        materials_ = std::make_unique<MaterialLibrary>(
            workspace.types(), workspace.files(),
            [&workspace]() -> const AssetIndex&
            {
                return workspace.catalog().index;
            },
            programs, std::move(textures));
        materials_->set_default_material(defaults);
        world_ = std::make_unique<World>();
        SceneAssemblyServices services;
        services.load_environment = [this, &workspace](const AssetRef& reference, std::string& error) -> TextureRef
        {
            const auto loaded = load_environment_asset(workspace.files(), workspace.catalog().index, reference);
            if (!loaded.succeeded())
            {
                error = loaded.status().message;
                return {};
            }
            return loaded.value();
        };
        services.load_mesh = [this, &workspace](const SceneMeshData& mesh, std::string& error) -> StaticMeshRef
        {
            if (!mesh.builtin_mesh.empty())
            {
                return geometry_.instantiate(mesh.builtin_mesh);
            }
            const auto source = std::find_if(mesh.resources.begin(), mesh.resources.end(),
                                             [](const SceneResourceBinding& value)
                                             {
                                                 return value.role == "mesh";
                                             });
            const auto* location =
                source == mesh.resources.end() ? nullptr : workspace.catalog().index.find(source->reference.asset_id);
            if (!location)
            {
                error = "Play scene mesh asset is missing.";
                return {};
            }
            const auto loaded = read_static_mesh_asset(workspace.files(), location->path);
            if (!loaded.succeeded())
            {
                error = loaded.status().message;
                return {};
            }
            return create_static_mesh_from_asset(loaded.value(), geometry_.default_material(),
                                                 [this](const AssetRef& reference)
                                                 {
                                                     return materials_->load(reference);
                                                 });
        };
        services.load_skeletal_mesh = [this, &workspace](const SceneSkeletalMeshData& mesh)
        {
            return load_skeletal_mesh_assets(workspace.types(), workspace.files(), workspace.catalog().index, mesh,
                                             geometry_.default_material(),
                                             [this](const AssetRef& reference)
                                             {
                                                 return materials_->load(reference);
                                             });
        };
        services.assign_material = [this](Actor&, MeshComponent& component, const std::string& slot,
                                          const AssetRef& reference, std::string& error)
        {
            const auto loaded = materials_->load(reference);
            if (!loaded.succeeded())
            {
                error = loaded.status().message;
                return false;
            }
            const auto& slots = component.material_slot_names();
            const auto found = std::find(slots.begin(), slots.end(), slot);
            if (found == slots.end())
            {
                error = "Unknown Play material slot: " + slot;
                return false;
            }
            return component.set_material_override(static_cast<std::uint32_t>(found - slots.begin()), loaded.value());
        };
        SceneAssemblyResult result;
        if (!assemble_scene(*world_, data, actors, workspace.types(), services, result, error_,
                            &workspace.catalog().index))
        {
            stop();
            return false;
        }
        world_->initialize();
        feedback_ = std::make_shared<SceneRenderFeedback>();
        if (!world_->bind_scene(scene))
        {
            error_ = "Play World could not bind its rendering scene.";
            stop();
            return false;
        }
        preparation_seconds_ = 0.0;
        state_ = EditorPlayState::Starting;
        return true;
    }

    void EditorPlaySession::tick(double delta_seconds)
    {
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0)
        {
            error_ = "Invalid Play delta time.";
            stop();
            return;
        }
        if (state_ == EditorPlayState::Starting)
        {
            const auto prepared = feedback_->state.load(std::memory_order_acquire);
            if (prepared == SceneRenderState::Failed)
            {
                error_ = "Play rendering preparation: " + feedback_->error;
                stop();
            }
            else if (prepared == SceneRenderState::Ready)
            {
                world_->begin_play();
                state_ = EditorPlayState::Playing;
                TOY_LOG_INFO("Play In Editor started with {} Actors.", world_->actor_count());
                // Do not charge initialization/first-frame waiting to gameplay.
            }
            else
            {
                preparation_seconds_ += delta_seconds;
                constexpr double preparation_timeout_seconds = 30.0;
                if (preparation_seconds_ > preparation_timeout_seconds)
                {
                    error_ = "Play could not prepare a visible scene frame within 30 seconds.";
                    stop();
                }
            }
        }
        else if (state_ == EditorPlayState::Playing && !world_->tick(delta_seconds))
        {
            error_ = "Play World tick failed.";
            stop();
        }
    }

    void EditorPlaySession::pause()
    {
        if (state_ == EditorPlayState::Playing)
        {
            state_ = EditorPlayState::Paused;
        }
    }
    void EditorPlaySession::resume()
    {
        if (state_ == EditorPlayState::Paused)
        {
            state_ = EditorPlayState::Playing;
        }
    }
    bool EditorPlaySession::stop()
    {
        const bool had_play = active();
        state_ = EditorPlayState::Stopped;
        action_ = EditorPlayAction::None;
        if (world_)
        {
            world_->end_play();
            if (world_->scene_interface())
            {
                world_->unbind_scene();
            }
            // World destruction also removes Actors spawned by end_play hooks.
            world_.reset();
        }
        if (input_session_owned_)
        {
            InputSystem::get_instance().end_play_session();
            input_session_owned_ = false;
        }
        if (materials_)
        {
            const auto drained = flush_rendering_commands();
            if (!drained.succeeded())
            {
                error_ = "Play scene rendering drain failed: " + drained.framework_status().message;
                TOY_LOG_ERROR("{}", error_);
            }
            materials_->shutdown();
            materials_.reset();
        }
        geometry_.release();
        feedback_.reset();
        if (had_play)
        {
            TOY_LOG_INFO("Play In Editor stopped; author scene retained.");
        }
        return error_.empty();
    }
    bool EditorPlaySession::active() const
    {
        return state_ != EditorPlayState::Stopped;
    }
    EditorPlayState EditorPlaySession::state() const
    {
        return state_;
    }
    const World* EditorPlaySession::world() const
    {
        return world_.get();
    }
    const std::string& EditorPlaySession::error() const
    {
        return error_;
    }
    std::shared_ptr<SceneRenderFeedback> EditorPlaySession::feedback() const
    {
        return feedback_;
    }
    void EditorPlaySession::request(EditorPlayAction action)
    {
        // Stop wins when multiple UI routes request a transition in one frame.
        if (action_ != EditorPlayAction::Stop)
        {
            action_ = action;
        }
    }
    EditorPlayAction EditorPlaySession::take_action()
    {
        const auto result = action_;
        action_ = EditorPlayAction::None;
        return result;
    }
} // namespace toy3d
