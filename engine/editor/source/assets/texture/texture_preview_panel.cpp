#include "assets/texture/texture_preview_panel.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

#include "imgui.h"
#include "logging/logger.h"
#include "threading/task_graph/graph_task.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        const char* channel_name(TexturePreviewChannel channel)
        {
            switch (channel)
            {
            case TexturePreviewChannel::RGBA:
                return "RGBA";
            case TexturePreviewChannel::Red:
                return "R";
            case TexturePreviewChannel::Green:
                return "G";
            case TexturePreviewChannel::Blue:
                return "B";
            case TexturePreviewChannel::Alpha:
                return "A";
            }
            return "";
        }

    } // namespace

    // --------------------------------------------------------------------------
    // CpuResult: worker-owned decoded texture and one requested display image
    // --------------------------------------------------------------------------
    struct TexturePreviewPanel::CpuResult
    {
        std::uint64_t revision = 0;
        AssetId asset_id;
        std::string path;
        std::uint32_t mip = 0;
        TexturePreviewChannel channel = TexturePreviewChannel::RGBA;
        std::shared_ptr<const Texture2DAsset> asset;
        Extent extent;
        std::vector<std::uint8_t> pixels;
        std::string error;
    };

    // --------------------------------------------------------------------------
    // TexturePreviewPanel: active asset, UI image lifetime and view interaction
    // --------------------------------------------------------------------------
    TexturePreviewPanel::TexturePreviewPanel(EditorWorkspace& workspace) : workspace_(workspace)
    {
    }
    TexturePreviewPanel::~TexturePreviewPanel()
    {
        shutdown();
    }

    void TexturePreviewPanel::request_open(const AssetId& id, bool focus)
    {
        if (!id.valid())
        {
            return;
        }
        open_ = true;
        focus_requested_ = focus;
        if (requested_asset_id_ == id)
        {
            if (!asset_ && !cpu_task_ && !candidate_id_.valid())
            {
                needs_prepare_ = true;
            }
            return;
        }
        requested_asset_id_ = id;
        error_.clear();
        requested_mip_ = id == asset_id_ ? mip_ : 0;
        requested_channel_ = id == asset_id_ ? channel_ : TexturePreviewChannel::RGBA;
        reload_requested_ = !(id == asset_id_);
        ++revision_;
        needs_prepare_ = true;
    }

    void TexturePreviewPanel::invalidate()
    {
        if (!open_)
        {
            return;
        }
        reload_requested_ = true;
        ++revision_;
        needs_prepare_ = true;
    }

    void TexturePreviewPanel::set_channel(TexturePreviewChannel channel)
    {
        if (requested_channel_ == channel)
        {
            return;
        }
        requested_channel_ = channel;
        ++revision_;
        needs_prepare_ = true;
    }

    void TexturePreviewPanel::set_mip(std::uint32_t mip)
    {
        if (requested_mip_ == mip)
        {
            return;
        }
        requested_mip_ = mip;
        ++revision_;
        needs_prepare_ = true;
    }

    void TexturePreviewPanel::tick()
    {
        if (cpu_task_ && cpu_task_->is_complete())
        {
            auto result = std::move(cpu_result_);
            if (cpu_task_->get_outcome() != TaskOutcome::Succeeded && result->error.empty())
            {
                result->error = "Texture preview worker failed.";
            }
            cpu_task_.reset();
            if (open_ && result->revision == revision_)
            {
                if (!result->error.empty())
                {
                    reject_preview(std::move(result->error));
                }
                else
                {
                    if (next_texture_ == std::numeric_limits<std::uint64_t>::max() ||
                        next_request_ == std::numeric_limits<std::uint64_t>::max())
                    {
                        reject_preview("Texture preview ID space exhausted.");
                    }
                    else
                    {
                        candidate_id_ = ImGuiTextureId(next_texture_++);
                        candidate_revision_ = revision_;
                        pending_work_.uploads.push_back(
                            {next_request_++, candidate_id_, result->extent, std::move(result->pixels)});
                        candidate_result_ = std::move(result);
                    }
                }
            }
        }
        if (!open_ || !needs_prepare_ || cpu_task_ || candidate_id_.valid())
        {
            return;
        }
        const AssetLocation* location = workspace_.catalog().index.find(requested_asset_id_);
        if (!location || location->index.root_type != "toy3d.Texture2DAssetData")
        {
            reject_preview("Texture asset is missing from the catalog.");
            return;
        }
        if (!TaskGraphInterface::is_running())
        {
            reject_preview("Texture preview requires a running Task Graph.");
            return;
        }
        auto result = std::make_shared<CpuResult>();
        result->revision = revision_;
        result->asset_id = requested_asset_id_;
        result->path = location->path.utf8();
        result->mip = requested_mip_;
        result->channel = requested_channel_;
        cpu_result_ = result;
        const auto path = location->path;
        auto existing = !reload_requested_ && requested_asset_id_ == asset_id_ ? asset_ : nullptr;
        FileSystem* files = &workspace_.files();
        const auto mip = requested_mip_;
        const auto channel = requested_channel_;
        try
        {
            cpu_task_ = dispatch_graph_task(
                TaskGraphInterface::get(), "Load texture preview",
                [result, existing, files, path, mip, channel](NamedThread, const GraphEventRef&)
                {
                    try
                    {
                        if (existing)
                        {
                            result->asset = existing;
                        }
                        else
                        {
                            auto read = read_texture_asset(*files, path);
                            if (!read.succeeded())
                            {
                                result->error = read.status().message;
                                return;
                            }
                            result->asset = std::make_shared<Texture2DAsset>(std::move(read.value()));
                        }
                        if (!make_texture_preview_pixels(*result->asset, mip, channel, result->extent.width,
                                                         result->extent.height, result->pixels, result->error) &&
                            result->error.empty())
                        {
                            result->error = "Could not prepare the requested texture preview.";
                        }
                    }
                    catch (const std::exception& exception)
                    {
                        result->error = exception.what();
                    }
                });
            needs_prepare_ = false;
            error_.clear();
        }
        catch (const std::exception& exception)
        {
            cpu_result_.reset();
            reject_preview(exception.what());
        }
    }

    void TexturePreviewPanel::reject_preview(std::string error)
    {
        error_ = std::move(error);
        needs_prepare_ = false;
        requested_asset_id_ = asset_id_;
        requested_mip_ = mip_;
        requested_channel_ = channel_;
        reload_requested_ = false;
        TOY_LOG_ERROR("Texture preview: {}", error_);
    }

    void TexturePreviewPanel::on_texture_result(UiTextureResult result)
    {
        if (!candidate_id_.valid() || result.texture_id != candidate_id_)
        {
            return;
        }
        const ImGuiTextureId completed = candidate_id_;
        candidate_id_ = {};
        auto candidate = std::move(candidate_result_);
        if (!result.succeeded() || !open_ || candidate_revision_ != revision_)
        {
            pending_work_.retire_textures.push_back(completed);
            if (!result.succeeded() && candidate_revision_ == revision_)
            {
                reject_preview(std::move(result.error));
            }
            return;
        }
        if (texture_id_.valid())
        {
            pending_work_.retire_textures.push_back(texture_id_);
        }
        texture_id_ = completed;
        image_extent_ = result.extent;
        if (!(asset_id_ == candidate->asset_id) || mip_ != candidate->mip)
        {
            zoom_ = pan_x_ = pan_y_ = 0.0f;
        }
        asset_id_ = candidate->asset_id;
        path_ = std::move(candidate->path);
        asset_ = std::move(candidate->asset);
        mip_ = candidate->mip;
        channel_ = candidate->channel;
        reload_requested_ = false;
        error_.clear();
    }

    void TexturePreviewPanel::collect_render_work(UiRenderWork& work)
    {
        for (auto& upload : pending_work_.uploads)
        {
            work.uploads.push_back(std::move(upload));
        }
        for (const auto id : pending_work_.retire_textures)
        {
            work.retire_textures.push_back(id);
        }
        pending_work_ = {};
    }

    std::vector<ImGuiTextureId> TexturePreviewPanel::texture_ids() const
    {
        return texture_id_.valid() ? std::vector<ImGuiTextureId>{texture_id_} : std::vector<ImGuiTextureId>{};
    }

    void TexturePreviewPanel::close()
    {
        open_ = false;
        ++revision_;
        if (texture_id_.valid())
        {
            pending_work_.retire_textures.push_back(texture_id_);
        }
        texture_id_ = {};
        image_extent_ = {};
        asset_.reset();
        asset_id_ = {};
        requested_asset_id_ = {};
        needs_prepare_ = false;
    }

    void TexturePreviewPanel::shutdown()
    {
        if (cpu_task_ && TaskGraphInterface::is_running())
        {
            const auto waited = TaskGraphInterface::get().wait_until_task_completes(cpu_task_, NamedThread::GameThread);
            if (!waited.succeeded())
            {
                TOY_LOG_ERROR("Texture preview worker could not finish during shutdown.");
            }
        }
        cpu_task_.reset();
        cpu_result_.reset();
        candidate_result_.reset();
        asset_.reset();
        pending_work_ = {};
        candidate_id_ = {};
        texture_id_ = {};
        open_ = false;
        asset_id_ = {};
        requested_asset_id_ = {};
        needs_prepare_ = false;
        reload_requested_ = false;
        path_.clear();
        error_.clear();
        image_extent_ = {};
        focus_requested_ = false;
    }

    void TexturePreviewPanel::draw()
    {
        if (!open_)
        {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(760, 650), ImGuiCond_FirstUseEver);
        if (focus_requested_)
        {
            ImGui::SetNextWindowFocus();
            focus_requested_ = false;
        }
        bool visible = true;
        // Begin may report drawable content even after the close button clears visible.
        // Skip image commands before close() removes their registered identity.
        if (ImGui::Begin("Texture Preview", &visible) && visible)
        {
            ImGui::TextWrapped("%s", path_.empty() ? "Loading Texture2D..." : path_.c_str());
            if (asset_)
            {
                ImGui::Text("%u x %u  |  %s  |  %u Mips", asset_->width, asset_->height,
                            asset_->usage == TextureUsage::Color
                                ? "Color (sRGB)"
                                : (asset_->usage == TextureUsage::Normal ? "Normal (+Y)" : "Linear Data"),
                            static_cast<unsigned>(asset_->mips.size()));
            }
            if (asset_ && reimport_callback_ && path_.compare(0u, 9u, "/Project/") == 0 && ImGui::Button("Reimport..."))
            {
                reimport_callback_(asset_id_, {asset_->usage, asset_->flip_green});
            }
            if (!error_.empty())
            {
                ImGui::TextWrapped("Preview: %s", error_.c_str());
            }
            if (cpu_task_ || candidate_id_.valid() || needs_prepare_)
            {
                ImGui::TextDisabled("Loading preview...");
            }
            ImGui::BeginDisabled(!(requested_asset_id_ == asset_id_) || !asset_);
            for (const auto channel :
                 {TexturePreviewChannel::RGBA, TexturePreviewChannel::Red, TexturePreviewChannel::Green,
                  TexturePreviewChannel::Blue, TexturePreviewChannel::Alpha})
            {
                if (channel != TexturePreviewChannel::RGBA)
                {
                    ImGui::SameLine();
                }
                if (ImGui::RadioButton(channel_name(channel), requested_channel_ == channel))
                {
                    set_channel(channel);
                }
            }
            if (asset_ && ImGui::BeginCombo("Mip", std::to_string(mip_).c_str()))
            {
                for (std::uint32_t level = 0; level < asset_->mips.size(); ++level)
                {
                    if (ImGui::Selectable(std::to_string(level).c_str(), mip_ == level))
                    {
                        set_mip(level);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            if (ImGui::Button("Fit"))
            {
                zoom_ = pan_x_ = pan_y_ = 0.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("100%"))
            {
                zoom_ = 1.0f;
                pan_x_ = pan_y_ = 0.0f;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Wheel: zoom  |  Middle drag: pan");
            ImVec2 canvas_size = ImGui::GetContentRegionAvail();
            canvas_size.x = std::max(1.0f, canvas_size.x);
            canvas_size.y = std::max(1.0f, canvas_size.y);
            const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("Texture Canvas", canvas_size);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 canvas_max(canvas_min.x + canvas_size.x, canvas_min.y + canvas_size.y);
            draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(34, 36, 40, 255));
            if (texture_id_.valid() && image_extent_.width && image_extent_.height)
            {
                const float fit = std::min(canvas_size.x / image_extent_.width, canvas_size.y / image_extent_.height);
                float scale = zoom_ > 0.0f ? zoom_ : fit;
                const ImVec2 center(canvas_min.x + canvas_size.x * 0.5f, canvas_min.y + canvas_size.y * 0.5f);
                if (ImGui::IsItemHovered())
                {
                    const float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f)
                    {
                        const ImVec2 mouse = ImGui::GetIO().MousePos;
                        const float next =
                            std::max(0.02f, std::min(64.0f, scale * (wheel > 0.0f ? 1.2f : 1.0f / 1.2f)));
                        pan_x_ = mouse.x - center.x - (mouse.x - center.x - pan_x_) * next / scale;
                        pan_y_ = mouse.y - center.y - (mouse.y - center.y - pan_y_) * next / scale;
                        zoom_ = scale = next;
                    }
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
                    {
                        pan_x_ += ImGui::GetIO().MouseDelta.x;
                        pan_y_ += ImGui::GetIO().MouseDelta.y;
                    }
                }
                const ImVec2 image_size(image_extent_.width * scale, image_extent_.height * scale);
                const ImVec2 image_min(center.x - image_size.x * 0.5f + pan_x_,
                                       center.y - image_size.y * 0.5f + pan_y_);
                const ImVec2 image_max(image_min.x + image_size.x, image_min.y + image_size.y);
                draw->PushClipRect(canvas_min, canvas_max, true);
                draw->AddRectFilled(image_min, image_max, IM_COL32(188, 188, 188, 255));
                constexpr float checker = 24.0f;
                const ImVec2 clipped_min(std::max(canvas_min.x, image_min.x), std::max(canvas_min.y, image_min.y));
                const ImVec2 clipped_max(std::min(canvas_max.x, image_max.x), std::min(canvas_max.y, image_max.y));
                if (clipped_min.x < clipped_max.x && clipped_min.y < clipped_max.y)
                {
                    const int first_x = static_cast<int>((clipped_min.x - image_min.x) / checker);
                    const int first_y = static_cast<int>((clipped_min.y - image_min.y) / checker);
                    const int last_x = static_cast<int>((clipped_max.x - image_min.x) / checker);
                    const int last_y = static_cast<int>((clipped_max.y - image_min.y) / checker);
                    for (int y = first_y; y <= last_y; ++y)
                    {
                        for (int x = first_x; x <= last_x; ++x)
                        {
                            if ((x + y) % 2 == 0)
                            {
                                draw->AddRectFilled(ImVec2(image_min.x + x * checker, image_min.y + y * checker),
                                                    ImVec2(std::min(image_max.x, image_min.x + (x + 1) * checker),
                                                           std::min(image_max.y, image_min.y + (y + 1) * checker)),
                                                    IM_COL32(108, 108, 108, 255));
                            }
                        }
                    }
                }
                draw->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(texture_id_.value())),
                               image_min, image_max);
                draw->PopClipRect();
            }
        }
        ImGui::End();
        if (!visible)
        {
            close();
        }
    }
} // namespace toy3d
