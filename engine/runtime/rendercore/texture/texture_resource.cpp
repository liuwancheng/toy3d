#include "rendercore/texture/texture_resource.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/render_resource_manager.h"

namespace toy3d
{
    namespace
    {
        bool has_same_native_layout(const TextureDesc& left, const TextureDesc& right)
        {
            return left.width == right.width && left.height == right.height && left.format == right.format &&
                   left.mip_pixels.size() == right.mip_pixels.size() && left.cube == right.cube;
        }

        bool is_deterministic_failure(const RHIStatus& status)
        {
            return status.code() == RHIErrorCode::InvalidArgument || status.code() == RHIErrorCode::Unsupported;
        }

        RHITextureDesc make_rhi_texture_desc(const TextureDesc& desc)
        {
            RHITextureDesc rhi_desc;
            rhi_desc.dimension = RHIResourceDimension::Texture2D;
            rhi_desc.width = desc.width;
            rhi_desc.height = desc.height;
            rhi_desc.depth = 1;
            rhi_desc.array_layers = desc.cube ? 6u : 1u;
            rhi_desc.cube_compatible = desc.cube;
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
            view_desc.dimension = desc.cube ? RHITextureViewDimension::TextureCube : RHITextureViewDimension::Texture2D;
            view_desc.format = desc.format;
            view_desc.subresources.aspect = RHITextureAspect::Color;
            view_desc.subresources.first_mip = 0;
            view_desc.subresources.mip_count = static_cast<std::uint32_t>(desc.mip_pixels.size());
            view_desc.subresources.first_layer = 0;
            view_desc.subresources.layer_count = desc.cube ? 6u : 1u;
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
            range.layer_count = desc.cube ? 6u : 1u;
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
                for (std::uint32_t face = 0u; face < (desc.cube ? 6u : 1u); ++face)
                {
                    RHITextureUploadDesc upload;
                    upload.destination.texture = texture;
                    upload.destination.mip = static_cast<std::uint32_t>(mip);
                    upload.destination.layer = face;
                    upload.extent.width = std::max(1U, desc.width >> static_cast<std::uint32_t>(mip));
                    upload.extent.height = std::max(1U, desc.height >> static_cast<std::uint32_t>(mip));
                    upload.extent.depth = 1;
                    upload.source.data =
                        desc.mip_pixels[mip].data() + static_cast<std::size_t>(face) * desc.slice_pitches[mip];
                    upload.source.size = desc.cube ? desc.slice_pitches[mip] : desc.mip_pixels[mip].size();
                    upload.source.row_pitch = desc.row_pitches[mip];
                    upload.source.slice_pitch = desc.slice_pitches[mip];
                    status = context.upload_texture(upload);
                    if (!status)
                    {
                        return status;
                    }
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

    // --------------------------------------------------------------------------
    // Texture: CPU 资产身份与资源表示所有权
    // --------------------------------------------------------------------------
    Texture::Texture(TextureDesc desc)
        : desc_(std::move(desc)), texture_resource_(std::make_shared<TextureResource>(desc_))
    {
    }

    Texture::~Texture() = default;

    Texture::Texture(Texture&& other) noexcept
        : desc_(std::move(other.desc_)), texture_resource_(std::move(other.texture_resource_))
    {
    }

    // --------------------------------------------------------------------------
    // TextureResource: 纹理上传、更新与可回收驻留
    // --------------------------------------------------------------------------
    TextureResource::TextureResource(const TextureDesc& initial_desc) : initial_desc_(initial_desc)
    {
    }

    RHIStatus TextureResource::update(TextureDesc desc, RenderResourceManager& manager)
    {
        std::string error;
        if (!desc.validate(error))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Invalid Texture update: " + error);
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

    TextureUsage TextureResource::usage_for_current_recording() const noexcept
    {
        return has_pending_update_ ? pending_desc_.usage
                                   : (has_active_desc_ ? active_desc_.usage : initial_desc_.usage);
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

        if (upload_desc.requires_linear_filter &&
            !EnumHasAllFlags(device.format_capabilities(upload_desc.format).usage,
                             RHIFormatUsage::Sampled | RHIFormatUsage::LinearFilter))
        {
            const auto unsupported = RHIStatus::failure(
                RHIErrorCode::Unsupported, "Texture requires sampling with linear filtering on this device/profile.");
            deterministic_recording_failure_ = true;
            return initial_upload ? fail(unsupported) : unsupported;
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
            if (binding_generation_ < std::numeric_limits<std::uint64_t>::max())
            {
                ++binding_generation_;
            }
        }
        // Keep the latest committed pixels as the source for future residency.
        if (has_pending_update_)
        {
            active_desc_ = std::move(pending_desc_);
        }
        else if (!has_active_desc_)
        {
            active_desc_ = std::move(initial_desc_);
        }
        has_active_desc_ = true;
        pending_desc_ = TextureDesc{};
        has_pending_update_ = false;
        pending_replacement_ = false;
        deterministic_recording_failure_ = false;
    }

    void TextureResource::on_recording_discarded() noexcept
    {
        candidate_view_.reset();
        candidate_texture_.reset();
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
        if (active_view_ && binding_generation_ < std::numeric_limits<std::uint64_t>::max())
        {
            ++binding_generation_;
        }
        active_view_.reset();
        active_texture_.reset();
        if (has_active_desc_)
        {
            initial_desc_ = std::move(active_desc_);
        }
        active_desc_ = TextureDesc{};
        pending_desc_ = TextureDesc{};
        has_active_desc_ = false;
        has_pending_update_ = false;
        pending_replacement_ = false;
        deterministic_recording_failure_ = false;
    }
} // namespace toy3d
