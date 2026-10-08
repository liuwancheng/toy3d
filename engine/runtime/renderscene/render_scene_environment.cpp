#include "renderscene/render_scene.h"

#include <cassert>
#include <utility>

#include "drivers/rhi/rhi_device.h"
#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "rendercore/render_resource_manager.h"
#include "rendercore/texture/texture_resource.h"

namespace toy3d
{
    void RenderScene::update_environment(SceneEnvironmentSnapshot environment)
    {
        enqueue_render_command("UpdateSceneEnvironment",
                               [this, environment = std::move(environment)]() mutable noexcept
                               {
                                   assert(is_on_logical_rendering_thread());
                                   std::string error;
                                   Quaternion rotation;
                                   if (!validate_scene_environment_snapshot(environment, error) ||
                                       !try_normalize(environment.rotation, rotation))
                                   {
                                       environment_error_ = RHIStatus::failure(RHIErrorCode::InvalidArgument, error);
                                       return;
                                   }
                                   environment.rotation = rotation;
                                   environment_error_ = RHIStatus::success();
                                   pending_environment_resource_ = {};
                                   pending_environment_ = std::move(environment);
                                   has_pending_environment_ = true;
                                   if (pending_environment_.cube && pending_environment_.cube != environment_.cube)
                                   {
                                       auto acquired =
                                           resource_manager_.acquire(*pending_environment_.cube->texture_resource());
                                       if (!acquired)
                                       {
                                           has_pending_environment_ = false;
                                           pending_environment_ = {};
                                           environment_error_ = acquired.status();
                                       }
                                       else
                                       {
                                           pending_environment_resource_ = std::move(acquired).value();
                                       }
                                   }
                               });
    }

    const SceneEnvironmentSnapshot& RenderScene::environment_for_current_recording() const
    {
        assert(is_on_logical_rendering_thread());
        return has_pending_environment_ ? pending_environment_ : environment_;
    }

    RHITextureViewRef RenderScene::environment_view_for_current_recording() const
    {
        assert(is_on_logical_rendering_thread());
        if (!environment_for_current_recording().cube)
        {
            return nullptr;
        }
        const auto* resource =
            pending_environment_resource_ ? pending_environment_resource_.get() : environment_resource_.get();
        return resource ? resource->view_for_current_recording() : nullptr;
    }

    RHIResult<RHISamplerRef> RenderScene::environment_sampler(RHIDevice& device)
    {
        assert(is_on_logical_rendering_thread());
        if (!environment_sampler_)
        {
            RHISamplerDesc sampler;
            sampler.address_u = sampler.address_v = sampler.address_w = RHIAddressMode::ClampToEdge;
            sampler.debug_name = "Scene.Environment.TrilinearClamp";
            const auto created = device.create_sampler(sampler);
            if (!created)
            {
                return created;
            }
            environment_sampler_ = created.value();
        }
        if (!environment_sampler_->is_owned_by(device))
        {
            return RHIResult<RHISamplerRef>::failure(RHIErrorCode::InvalidArgument,
                                                     "Environment sampler belongs to another device.");
        }
        return RHIResult<RHISamplerRef>::success(environment_sampler_);
    }

    RHIStatus RenderScene::environment_status() const
    {
        assert(is_on_logical_rendering_thread());
        if (!environment_error_)
        {
            return environment_error_;
        }
        const auto* resource =
            pending_environment_resource_ ? pending_environment_resource_.get() : environment_resource_.get();
        if (resource && resource->state() == RenderResourceState::Failed)
        {
            return resource->failure_status();
        }
        if (environment_for_current_recording().cube && !environment_view_for_current_recording())
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Scene Environment upload is preparing.");
        }
        return RHIStatus::success();
    }

    void RenderScene::resolve_environment_recording(bool committed)
    {
        assert(is_on_logical_rendering_thread());
        if (!has_pending_environment_)
        {
            return;
        }
        if (!committed)
        {
            if (pending_environment_resource_ && pending_environment_resource_->state() == RenderResourceState::Failed)
            {
                environment_error_ = pending_environment_resource_->failure_status();
                pending_environment_resource_ = {};
                pending_environment_ = {};
                has_pending_environment_ = false;
            }
            // Retryable frame failures retain the immutable candidate payload.
            return;
        }
        if (pending_environment_resource_ && pending_environment_resource_->state() != RenderResourceState::Ready)
        {
            environment_error_ =
                RHIStatus::failure(RHIErrorCode::NotReady, "Environment publication requires committed GPU upload.");
            return;
        }
        if (pending_environment_.cube != environment_.cube)
        {
            environment_resource_ = std::move(pending_environment_resource_);
        }
        environment_ = std::move(pending_environment_);
        pending_environment_ = {};
        has_pending_environment_ = false;
    }
} // namespace toy3d
