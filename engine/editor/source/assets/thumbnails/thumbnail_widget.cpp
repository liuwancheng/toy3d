#include "assets/thumbnails/thumbnail_widget.h"

namespace toy3d
{
    void draw_asset_placeholder(ImDrawList& draw, ImVec2 position, float size, bool folder, const std::string& type)
    {
        const ImU32 color = folder ? IM_COL32(190, 151, 74, 255) : IM_COL32(116, 149, 181, 255);
        const ImVec2 a(position.x + size * 0.2f, position.y + size * 0.3f);
        const ImVec2 b(position.x + size * 0.8f, position.y + size * 0.75f);
        if (folder)
        {
            draw.AddRectFilled(a, b, color, 4);
            draw.AddRectFilled(ImVec2(a.x, a.y - size * 0.09f), ImVec2(a.x + size * 0.25f, a.y + 4), color, 3);
        }
        else if (type == "toy3d.MaterialAssetData" || type == "toy3d.MaterialInstanceAssetData")
        {
            const ImVec2 center(position.x + size * 0.5f, position.y + size * 0.5f);
            draw.AddCircleFilled(center, size * 0.3f, IM_COL32(106, 151, 167, 255), 32);
            draw.AddCircleFilled(ImVec2(center.x - size * 0.07f, center.y - size * 0.07f), size * 0.21f,
                                 IM_COL32(146, 195, 208, 255), 32);
            if (type == "toy3d.MaterialInstanceAssetData")
            {
                draw.AddText(ImVec2(position.x + size * 0.7f, position.y + size * 0.7f), IM_COL32_WHITE, "MI");
            }
        }
        else if (type == "toy3d.SkeletonAssetData")
        {
            const ImU32 tint = IM_COL32(101, 203, 224, 255);
            const auto point = [position, size](float x, float y)
            {
                return ImVec2(position.x + size * x, position.y + size * y);
            };
            const ImVec2 joints[] = {point(0.5f, 0.2f),   point(0.5f, 0.32f),  point(0.5f, 0.56f),  point(0.27f, 0.35f),
                                     point(0.73f, 0.35f), point(0.23f, 0.48f), point(0.77f, 0.48f), point(0.32f, 0.7f),
                                     point(0.65f, 0.7f),  point(0.28f, 0.86f), point(0.74f, 0.85f)};
            const int segments[][2] = {{0, 1}, {1, 2}, {1, 3}, {1, 4}, {3, 5}, {4, 6}, {2, 7}, {2, 8}, {7, 9}, {8, 10}};
            for (const auto& segment : segments)
            {
                draw.AddLine(joints[segment[0]], joints[segment[1]], tint, size * 0.026f);
            }
            draw.AddCircleFilled(joints[0], size * 0.07f, tint, 16);
            for (const auto& joint : joints)
            {
                draw.AddCircleFilled(joint, size * 0.025f, IM_COL32_WHITE, 10);
            }
        }
        else if (type == "toy3d.Texture2DAssetData")
        {
            draw.AddRectFilled(a, b, IM_COL32(124, 151, 173, 255), 3);
            draw.AddRectFilled(ImVec2(a.x + 4, a.y + 4), ImVec2(b.x - 4, b.y - 4), IM_COL32(62, 85, 103, 255), 2);
            draw.AddCircleFilled(ImVec2(a.x + size * 0.17f, a.y + size * 0.16f), size * 0.055f,
                                 IM_COL32(232, 205, 128, 255));
            draw.AddTriangleFilled(ImVec2(a.x + 4, b.y - 4), ImVec2(a.x + size * 0.24f, a.y + size * 0.22f),
                                   ImVec2(a.x + size * 0.48f, b.y - 4), IM_COL32(122, 177, 143, 255));
        }
        else if (type == "toy3d.SceneAssetData")
        {
            draw.AddRectFilled(a, b, IM_COL32(57, 82, 105, 255), 4);
            draw.AddCircleFilled(ImVec2(a.x + size * 0.14f, a.y + size * 0.14f), size * 0.065f,
                                 IM_COL32(245, 199, 103, 255));
            draw.AddTriangleFilled(ImVec2(a.x + 4, b.y - 4), ImVec2(a.x + size * 0.25f, a.y + size * 0.2f),
                                   ImVec2(a.x + size * 0.47f, b.y - 4), IM_COL32(115, 163, 130, 255));
            draw.AddTriangleFilled(ImVec2(a.x + size * 0.25f, b.y - 4), ImVec2(a.x + size * 0.47f, a.y + size * 0.12f),
                                   ImVec2(b.x - 4, b.y - 4), IM_COL32(150, 185, 150, 255));
        }
        else
        {
            const ImVec2 top(position.x + size * 0.5f, position.y + size * 0.18f);
            const ImVec2 left(a.x, position.y + size * 0.4f);
            const ImVec2 right(b.x, left.y);
            const ImVec2 middle(top.x, position.y + size * 0.58f);
            const ImVec2 bottom(top.x, position.y + size * 0.85f);
            draw.AddQuadFilled(top, right, middle, left, color);
            draw.AddQuadFilled(left, middle, bottom, ImVec2(left.x, bottom.y - size * 0.18f),
                               IM_COL32(72, 103, 134, 255));
            draw.AddQuadFilled(middle, right, ImVec2(right.x, bottom.y - size * 0.18f), bottom,
                               IM_COL32(91, 127, 159, 255));
        }
    }

    void paint_asset_thumbnail(ImDrawList& draw, ImVec2 position, float size, const AssetThumbnailView& view,
                               const std::string& type)
    {
        const ImVec2 end(position.x + size, position.y + size);
        draw.AddRectFilled(position, end, IM_COL32(17, 17, 18, 255), 3.0f);
        if (view.texture_id.valid())
        {
            draw.AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(view.texture_id.value())),
                          ImVec2(position.x + 2, position.y + 2), ImVec2(end.x - 2, end.y - 3));
        }
        else
        {
            draw_asset_placeholder(draw, position, size, false, type);
        }
        const ImU32 accent = type == "toy3d.MaterialAssetData" || type == "toy3d.MaterialInstanceAssetData"
                                 ? IM_COL32(98, 185, 91, 255)
                             : type == "toy3d.Texture2DAssetData"         ? IM_COL32(194, 94, 94, 255)
                             : type == "toy3d.AnimationSequenceAssetData" ? IM_COL32(218, 173, 79, 255)
                             : type == "toy3d.SkeletonAssetData"          ? IM_COL32(169, 113, 194, 255)
                                                                          : IM_COL32(86, 187, 196, 255);
        draw.AddRect(position, end, IM_COL32(67, 67, 69, 255), 3.0f);
        draw.AddRectFilled(ImVec2(position.x + 2, end.y - 3), ImVec2(end.x - 2, end.y - 1), accent);
        if (view.busy || !view.error.empty())
        {
            const char* status = view.error.empty() ? "..." : "!";
            draw.AddRectFilled(ImVec2(position.x + 2, position.y + 2), ImVec2(position.x + 18, position.y + 18),
                               IM_COL32(20, 20, 20, 220), 2.0f);
            draw.AddText(ImVec2(position.x + 4, position.y + 2),
                         view.error.empty() ? IM_COL32_WHITE : IM_COL32(255, 130, 100, 255), status);
        }
    }
    bool asset_thumbnail_widget(const AssetThumbnailView& view, const std::string& type, float size, bool interactive)
    {
        const auto position = ImGui::GetCursorScreenPos();
        bool pressed = false;
        if (interactive)
        {
            pressed = ImGui::InvisibleButton("##Thumbnail", ImVec2(size, size));
        }
        else
        {
            ImGui::Dummy(ImVec2(size, size));
        }
        paint_asset_thumbnail(*ImGui::GetWindowDrawList(), position, size, view, type);
        if (!view.error.empty() && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", view.error.c_str());
        }
        return pressed;
    }
} // namespace toy3d
