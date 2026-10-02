#include "ui_texture_registry.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"

namespace toy3d
{
    RHIStatus UiTextureRegistry::create_target(RHIDevice& device, ImGuiTextureId id, Extent extent)
    {
        if (id.value() <= IMGUI_SCENE_VIEWPORT_TEXTURE_ID.value() || entries_.count(id.value()) || !extent.width ||
            !extent.height || extent.width > rhi_max_texture_readback_dimension ||
            extent.height > rhi_max_texture_readback_dimension)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "UI texture ID or bounded extent is invalid.");
        }
        RHITextureDesc desc;
        desc.width = extent.width;
        desc.height = extent.height;
        desc.format = PixelFormat::B8G8R8A8UNorm;
        desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource | RHIResourceUsage::CopySource |
                     RHIResourceUsage::CopyDestination;
        desc.initial_access = RHIAccess::Common;
        desc.debug_name = "UiImage";
        auto created = device.create_texture(desc);
        if (!created)
        {
            return created.status();
        }
        Entry entry;
        entry.texture = created.value();
        RHITextureViewDesc view;
        view.format = desc.format;
        view.type = RHIResourceViewType::ShaderResource;
        auto sampled = device.create_texture_view(entry.texture, view);
        if (!sampled)
        {
            return sampled.status();
        }
        entry.sampled_view = sampled.value();
        view.type = RHIResourceViewType::RenderTarget;
        auto target = device.create_texture_view(entry.texture, view);
        if (!target)
        {
            return target.status();
        }
        entry.target_view = target.value();
        entries_.emplace(id.value(), std::move(entry));
        return RHIStatus::success();
    }

    RHIStatus UiTextureRegistry::record_upload(RHIDevice& device, RHIGraphicsCommandContext& context,
                                               const UiTextureUpload& upload)
    {
        // Uploaded editor images can be full-resolution Texture2D assets.
        // Rendered thumbnail targets retain the separate 512-pixel readback bound.
        constexpr std::uint32_t max_uploaded_image_dimension = 4096;
        if (!upload.texture_id.valid() || upload.texture_id.value() <= IMGUI_SCENE_VIEWPORT_TEXTURE_ID.value() ||
            entries_.count(upload.texture_id.value()) || !upload.extent.width || !upload.extent.height ||
            upload.extent.width > max_uploaded_image_dimension || upload.extent.height > max_uploaded_image_dimension)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "UI upload image extent or ID is invalid.");
        }
        if (upload.bgra_pixels.size() != static_cast<std::uint64_t>(upload.extent.width) * upload.extent.height * 4)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "UI upload pixel count is invalid.");
        }
        RHITextureDesc image_desc;
        image_desc.width = upload.extent.width;
        image_desc.height = upload.extent.height;
        image_desc.format = PixelFormat::B8G8R8A8UNorm;
        image_desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
        image_desc.initial_access = RHIAccess::Common;
        image_desc.debug_name = "UiUploadedImage";
        auto image = device.create_texture(image_desc);
        if (!image)
        {
            return image.status();
        }
        RHITextureViewDesc view_desc;
        view_desc.format = image_desc.format;
        view_desc.type = RHIResourceViewType::ShaderResource;
        auto sampled = device.create_texture_view(image.value(), view_desc);
        if (!sampled)
        {
            return sampled.status();
        }
        Entry entry;
        entry.texture = image.value();
        entry.sampled_view = sampled.value();
        entries_.emplace(upload.texture_id.value(), std::move(entry));
        RHIStatus status = RHIStatus::success();
        RHIResourceTransition copy;
        copy.resource = texture(upload.texture_id);
        copy.before = RHIAccess::Common;
        copy.after = RHIAccess::CopyDestination;
        status = context.transition_resources({copy});
        if (!status)
        {
            return status;
        }
        RHITextureUploadDesc desc;
        desc.destination.texture = texture(upload.texture_id);
        desc.extent = {upload.extent.width, upload.extent.height, 1};
        desc.source.data = upload.bgra_pixels.data();
        desc.source.size = upload.bgra_pixels.size();
        desc.source.row_pitch = upload.extent.width * 4;
        desc.source.slice_pitch = desc.source.size;
        status = context.upload_texture(desc);
        if (!status)
        {
            return status;
        }
        copy.before = RHIAccess::CopyDestination;
        copy.after = RHIAccess::ShaderResourceGraphics;
        return context.transition_resources({copy});
    }

    const RHITextureRef& UiTextureRegistry::texture(ImGuiTextureId id) const
    {
        return entries_.at(id.value()).texture;
    }

    const RHITextureViewRef& UiTextureRegistry::target_view(ImGuiTextureId id) const
    {
        return entries_.at(id.value()).target_view;
    }

    std::vector<ImGuiTextureBinding> UiTextureRegistry::bindings() const
    {
        std::vector<ImGuiTextureBinding> result;
        for (const auto& entry : entries_)
        {
            result.push_back({ImGuiTextureId(entry.first), entry.second.sampled_view});
        }
        return result;
    }

    void UiTextureRegistry::retire(ImGuiTextureId id)
    {
        entries_.erase(id.value());
    }
    void UiTextureRegistry::clear()
    {
        entries_.clear();
    }
} // namespace toy3d
