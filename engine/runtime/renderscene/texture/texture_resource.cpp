#include "renderscene/texture/texture_resource.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "renderscene/render_resource_manager.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool has_same_native_layout(const TextureDesc& left, const TextureDesc& right)
        {
            return left.width == right.width && left.height == right.height && left.format == right.format &&
                   left.mip_pixels.size() == right.mip_pixels.size();
        }

        bool is_deterministic_failure(const RHIStatus& status)
        {
            return status.code() == RHIErrorCode::InvalidArgument || status.code() == RHIErrorCode::Unsupported;
        }

        void release_pixel_payload(TextureDesc& desc) noexcept
        {
            for (std::vector<std::uint8_t>& mip : desc.mip_pixels)
            {
                std::vector<std::uint8_t>().swap(mip);
            }
        }

        RHITextureDesc make_rhi_texture_desc(const TextureDesc& desc)
        {
            RHITextureDesc rhi_desc;
            rhi_desc.dimension = RHIResourceDimension::Texture2D;
            rhi_desc.width = desc.width;
            rhi_desc.height = desc.height;
            rhi_desc.depth = 1;
            rhi_desc.array_layers = 1;
            rhi_desc.mip_levels = static_cast<std::uint32_t>(desc.mip_pixels.size());
            rhi_desc.sample_count = 1;
            rhi_desc.format = desc.format;
            rhi_desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
            rhi_desc.cpu_access = RHICPUAccess::None;
            rhi_desc.initial_access = RHIAccess::Common;
            rhi_desc.debug_name = "Texture.Asset2D";
            return rhi_desc;
        }

        RHITextureViewDesc make_rhi_texture_view_desc(const TextureDesc& desc)
        {
            RHITextureViewDesc view_desc;
            view_desc.type = RHIResourceViewType::ShaderResource;
            view_desc.dimension = RHITextureViewDimension::Texture2D;
            view_desc.format = desc.format;
            view_desc.subresources.aspect = RHITextureAspect::Color;
            view_desc.subresources.first_mip = 0;
            view_desc.subresources.mip_count = static_cast<std::uint32_t>(desc.mip_pixels.size());
            view_desc.subresources.first_layer = 0;
            view_desc.subresources.layer_count = 1;
            view_desc.debug_name = "Texture.Asset2D.SRV";
            return view_desc;
        }

        RHISubresourceRange complete_subresources(const TextureDesc& desc)
        {
            RHISubresourceRange range;
            range.aspect = RHITextureAspect::Color;
            range.first_mip = 0;
            range.mip_count = static_cast<std::uint32_t>(desc.mip_pixels.size());
            range.first_layer = 0;
            range.layer_count = 1;
            return range;
        }

        RHIStatus record_texture_payload(RHIGraphicsCommandContext& context, const TextureDesc& desc,
                                         const RHITextureRef& texture, RHIAccess before_access)
        {
            RHIResourceTransition to_copy;
            to_copy.resource = texture;
            to_copy.subresources = complete_subresources(desc);
            to_copy.before = before_access;
            to_copy.after = RHIAccess::CopyDestination;
            RHIStatus status = context.transition_resources({to_copy});
            if (!status)
            {
                return status;
            }

            for (std::size_t mip = 0; mip < desc.mip_pixels.size(); ++mip)
            {
                RHITextureUploadDesc upload;
                upload.destination.texture = texture;
                upload.destination.mip = static_cast<std::uint32_t>(mip);
                upload.destination.layer = 0;
                upload.extent.width = std::max(1U, desc.width >> static_cast<std::uint32_t>(mip));
                upload.extent.height = std::max(1U, desc.height >> static_cast<std::uint32_t>(mip));
                upload.extent.depth = 1;
                upload.source.data = desc.mip_pixels[mip].data();
                upload.source.size = desc.mip_pixels[mip].size();
                upload.source.row_pitch = desc.row_pitches[mip];
                upload.source.slice_pitch = desc.slice_pitches[mip];
                status = context.upload_texture(upload);
                if (!status)
                {
                    return status;
                }
            }

            RHIResourceTransition to_sampled;
            to_sampled.resource = texture;
            to_sampled.subresources = complete_subresources(desc);
            to_sampled.before = RHIAccess::CopyDestination;
            to_sampled.after = RHIAccess::ShaderResourceGraphics;
            return context.transition_resources({to_sampled});
        }
    } // namespace

    Texture::Texture(TextureDesc desc)
        : desc_(std::move(desc)), texture_resource_(std::make_unique<TextureResource>(desc_))
    {
    }

    Texture::~Texture() = default;

    Texture::Texture(Texture&& other) noexcept
        : desc_(std::move(other.desc_)), texture_resource_(std::move(other.texture_resource_))
    {
    }

    void Texture::release(std::shared_ptr<const Texture>& texture)
    {
        if (!texture)
        {
            return;
        }
        if (texture.use_count() != 1)
        {
            throw std::invalid_argument("Texture final release requires the caller to hold the last TextureRef");
        }

        std::shared_ptr<const Texture> release_owner = texture;
        enqueue_render_command("ReleaseTextureResource",
                               [release_owner = std::move(release_owner)]() noexcept
                               {
                                   TextureResource* const resource = release_owner->texture_resource_.get();
                                   if (resource != nullptr)
                                   {
                                       resource->release_from_owner_manager();
                                   }
                               });

        texture.reset();
    }

    TextureResource::TextureResource(const TextureDesc& initial_desc) : initial_desc_(initial_desc) {}

    RHIStatus TextureResource::begin_init(RenderResourceManager& manager)
    {
        std::string error;
        if (!initial_desc_.validate(error))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Invalid TextureDesc: " + error);
        }
        const RHIStatus status = manager.begin_init(*this);
        if (status)
        {
            owner_manager_ = &manager;
        }
        return status;
    }

    RHIStatus TextureResource::update(TextureDesc desc, RenderResourceManager& manager)
    {
        std::string error;
        if (!desc.validate(error))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Invalid Texture update: " + error);
        }
        if (owner_manager_ != &manager)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture update requires its owning RenderResourceManager");
        }
        const RHIStatus status = manager.begin_update(*this);
        if (!status)
        {
            return status;
        }

        pending_replacement_ = !has_active_desc_ || !has_same_native_layout(active_desc_, desc);
        pending_desc_ = std::move(desc);
        has_pending_update_ = true;
        deterministic_recording_failure_ = false;
        return RHIStatus::success();
    }

    RHIStatus TextureResource::release(RenderResourceManager& manager)
    {
        if (owner_manager_ != nullptr && owner_manager_ != &manager)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture release requires its owning RenderResourceManager");
        }
        const RHIStatus status = manager.release(*this);
        if (status)
        {
            owner_manager_ = nullptr;
        }
        return status;
    }

    void TextureResource::release_from_owner_manager() noexcept
    {
        if (owner_manager_ == nullptr)
        {
            return;
        }
        const RHIStatus status = release(*owner_manager_);
        if (!status)
        {
            TOY_LOG_ERROR("TextureResource ownership-transfer release failed: {}", status.message());
            std::terminate();
        }
    }

    const RHITextureViewRef& TextureResource::view_for_current_recording() const noexcept
    {
        return candidate_view_ ? candidate_view_ : active_view_;
    }

    RHIStatus TextureResource::record_upload(RHIDevice& device, RHIGraphicsCommandContext& context)
    {
        deterministic_recording_failure_ = false;
        const bool initial_upload = state() == RenderResourceState::PendingUpload;
        const TextureDesc& upload_desc = has_pending_update_ ? pending_desc_ : initial_desc_;

        std::string validation_error;
        if (!upload_desc.validate(validation_error))
        {
            const RHIStatus invalid =
                RHIStatus::failure(RHIErrorCode::InvalidArgument, "Invalid Texture upload: " + validation_error);
            deterministic_recording_failure_ = true;
            return initial_upload ? fail(invalid) : invalid;
        }

        const bool create_candidate = initial_upload || pending_replacement_;
        RHITextureRef upload_texture = active_texture_;
        RHITextureViewRef upload_view = active_view_;
        if (create_candidate)
        {
            const RHITextureDesc rhi_desc = make_rhi_texture_desc(upload_desc);
            RHIResult<RHITextureRef> created_texture = device.create_texture(rhi_desc);
            if (!created_texture)
            {
                deterministic_recording_failure_ = is_deterministic_failure(created_texture.status());
                return initial_upload && deterministic_recording_failure_ ? fail(created_texture.status())
                                                                          : created_texture.status();
            }
            upload_texture = std::move(created_texture).value();

            RHIResult<RHITextureViewRef> created_view =
                device.create_texture_view(upload_texture, make_rhi_texture_view_desc(upload_desc));
            if (!created_view)
            {
                deterministic_recording_failure_ = is_deterministic_failure(created_view.status());
                return initial_upload && deterministic_recording_failure_ ? fail(created_view.status())
                                                                          : created_view.status();
            }
            upload_view = std::move(created_view).value();
        }
        else if (!upload_texture || !upload_view)
        {
            const RHIStatus invalid = RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                                         "Texture content update requires an active texture and view");
            deterministic_recording_failure_ = true;
            return invalid;
        }

        const RHIStatus recorded =
            record_texture_payload(context, upload_desc, upload_texture,
                                   create_candidate ? RHIAccess::Common : RHIAccess::ShaderResourceGraphics);
        if (!recorded)
        {
            deterministic_recording_failure_ = is_deterministic_failure(recorded);
            return initial_upload && deterministic_recording_failure_ ? fail(recorded) : recorded;
        }

        if (create_candidate)
        {
            candidate_texture_ = std::move(upload_texture);
            candidate_view_ = std::move(upload_view);
        }
        return RHIStatus::success();
    }

    void TextureResource::on_recording_committed() noexcept
    {
        if (candidate_texture_ && candidate_view_)
        {
            active_texture_ = std::move(candidate_texture_);
            active_view_ = std::move(candidate_view_);
            active_desc_ = has_pending_update_ ? std::move(pending_desc_) : std::move(initial_desc_);
            release_pixel_payload(active_desc_);
            has_active_desc_ = true;
            if (binding_generation_ < std::numeric_limits<std::uint64_t>::max())
            {
                ++binding_generation_;
            }
        }

        if (has_pending_update_)
        {
            pending_desc_ = TextureDesc{};
            has_pending_update_ = false;
        }
        pending_replacement_ = false;
        deterministic_recording_failure_ = false;
    }

    void TextureResource::on_recording_discarded() noexcept
    {
        candidate_view_.reset();
        candidate_texture_.reset();
        if (state() == RenderResourceState::Failed)
        {
            release_pixel_payload(initial_desc_);
        }
        if (has_pending_update_ && deterministic_recording_failure_)
        {
            pending_desc_ = TextureDesc{};
            has_pending_update_ = false;
            pending_replacement_ = false;
        }
        deterministic_recording_failure_ = false;
    }

    void TextureResource::release_rhi() noexcept
    {
        candidate_view_.reset();
        candidate_texture_.reset();
        active_view_.reset();
        active_texture_.reset();
        release_pixel_payload(initial_desc_);
        release_pixel_payload(active_desc_);
        release_pixel_payload(pending_desc_);
        has_active_desc_ = false;
        has_pending_update_ = false;
        pending_replacement_ = false;
        deterministic_recording_failure_ = false;
        binding_generation_ = 0;
    }
} // namespace toy3d
