#pragma once

#include "renderscene/ui/imgui_renderer.h"
#include "ui/ui_texture_work.h"

#include <map>

namespace toy3d
{
    class UiTextureRegistry final
    {
    public:
        RHIStatus create_target(RHIDevice& device, ImGuiTextureId id, Extent extent);
        RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context, const UiTextureUpload& upload);
        const RHITextureRef& texture(ImGuiTextureId id) const;
        const RHITextureViewRef& target_view(ImGuiTextureId id) const;
        std::vector<ImGuiTextureBinding> bindings() const;
        void retire(ImGuiTextureId id);
        void clear();

    private:
        struct Entry
        {
            RHITextureRef texture;
            RHITextureViewRef sampled_view;
            RHITextureViewRef target_view;
        };
        std::map<std::uint64_t, Entry> entries_;
    };
} // namespace toy3d
