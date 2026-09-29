#include "ui_texture_registry.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"

namespace toy3d
{
    RHIStatus UiTextureRegistry::create_target(RHIDevice& device, ImGuiTextureId id, Extent extent)
    {
        if (id.value() <= IMGUI_SCENE_VIEWPORT_TEXTURE_ID.value() || entries_.count(id.value()) ||
            !extent.width || !extent.height || extent.width > rhi_max_texture_readback_dimension || extent.height > rhi_max_texture_readback_dimension)
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "UI texture ID or bounded extent is invalid.");
        RHITextureDesc desc;
        desc.width = extent.width;
        desc.height = extent.height;
        desc.format = PixelFormat::B8G8R8A8UNorm;
        desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource |
            RHIResourceUsage::CopySource | RHIResourceUsage::CopyDestination;
        desc.initial_access = RHIAccess::Common;
        desc.debug_name = "UiImage";
        auto created = device.create_texture(desc);
        if (!created) return created.status();
        Entry entry;
        entry.texture = created.value();
        RHITextureViewDesc view;
        view.format = desc.format;
        view.type = RHIResourceViewType::ShaderResource;
        auto sampled = device.create_texture_view(entry.texture, view);
        if (!sampled) return sampled.status();
        entry.sampled_view = sampled.value();
        view.type = RHIResourceViewType::RenderTarget;
        auto target = device.create_texture_view(entry.texture, view);
        if (!target) return target.status();
        entry.target_view = target.value();
        entries_.emplace(id.value(), std::move(entry));
        return RHIStatus::success();
    }

    RHIStatus UiTextureRegistry::record_upload(RHIDevice& device, RHIGraphicsCommandContext& context,
                                              const UiTextureUpload& upload)
    {
        if (upload.bgra_pixels.size() != static_cast<std::uint64_t>(upload.extent.width) * upload.extent.height * 4)
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "UI upload pixel count is invalid.");
        RHIStatus status = create_target(device, upload.texture_id, upload.extent);
        if (!status) return status;
        RHIResourceTransition copy;
        copy.resource = texture(upload.texture_id);
        copy.before = RHIAccess::Common;
        copy.after = RHIAccess::CopyDestination;
        status = context.transition_resources({copy});
        if (!status) return status;
        RHITextureUploadDesc desc;
        desc.destination.texture = texture(upload.texture_id);
        desc.extent = {upload.extent.width, upload.extent.height, 1};
        desc.source.data = upload.bgra_pixels.data();
        desc.source.size = upload.bgra_pixels.size();
        desc.source.row_pitch = upload.extent.width * 4;
        desc.source.slice_pitch = desc.source.size;
        status = context.upload_texture(desc);
        if (!status) return status;
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
        for (const auto& entry : entries_) result.push_back({ImGuiTextureId(entry.first), entry.second.sampled_view});
        return result;
    }

    void UiTextureRegistry::retire(ImGuiTextureId id) { entries_.erase(id.value()); }
    void UiTextureRegistry::clear() { entries_.clear(); }
} // namespace toy3d
