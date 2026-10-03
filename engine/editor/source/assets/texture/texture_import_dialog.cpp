#include "assets/texture/texture_import_dialog.h"

#include "assets/texture/texture_asset_tools.h"
#include "file_system/virtual_path.h"
#include "imgui.h"
#include "logging/logger.h"
#include "platform/model_file_picker.h"
#include "platform/window_interface.h"
#include "scene/editor_selection.h"
#include "misc/utf8.h"
#include "threading/task_graph/graph_task.h"
#include "threading/task_graph/task_graph_interface.h"
#include "workspace/editor_workspace.h"

#include <algorithm>
#include <cstring>
#include <exception>

namespace toy3d
{
    namespace
    {
        bool destination(const std::string& source, const std::string& folder, const std::string& name,
                         std::string& path, std::string& error, bool environment)
        {
            if (folder != "/Project" && folder.compare(0, 9, "/Project/") != 0)
            {
                error = "Select a writable Project folder.";
                return false;
            }
            if (source.empty() || source.size() > maximum_file_drop_path_bytes || !is_valid_utf8(source) ||
                source.find('\0') != std::string::npos)
            {
                error = "Enter a valid UTF-8 source image path.";
                return false;
            }
            const auto dot = source.find_last_of('.');
            std::string extension = dot == std::string::npos ? "" : source.substr(dot);
            for (char& c : extension)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c + ('a' - 'A'));
                }
            }
            if (environment ? extension != ".hdr"
                            : (extension != ".png" && extension != ".jpg" && extension != ".jpeg"))
            {
                error = environment ? "Select a Radiance HDR panorama." : "Select a PNG or JPEG image.";
                return false;
            }
            if (name.empty() || name.size() >= 256u || !is_valid_utf8(name) ||
                name.find_first_of("<>:\"/\\|?*") != std::string::npos || name.back() == '.' || name.back() == ' ' ||
                std::any_of(name.begin(), name.end(),
                            [](unsigned char c)
                            {
                                return c < 32;
                            }))
            {
                error = "Enter a valid resource name without an extension.";
                return false;
            }
            path = folder + "/" + name + ".asset";
            const auto parsed = VirtualPath::parse(path);
            if (!parsed.succeeded())
            {
                error = parsed.status().message;
                return false;
            }
            error.clear();
            return true;
        }
    } // namespace

    void TextureImportDialog::clear()
    {
        candidates_.clear();
        failed_.clear();
        // A canceled worker owns only its result, but remains the single import
        // job until completion. Its bytes must never be published afterward.
        if (!task_ || task_->is_complete())
        {
            prepared_.reset();
            task_.reset();
        }
        reimport_path_.clear();
        reimport_baseline_.clear();
        reimport_id_ = {};
        environment_ = false;
        folder_.clear();
        error_.clear();
        next_ = 0;
        active_ = open_ = browse_ = running_ = false;
    }

    bool TextureImportDialog::request(const std::string& folder, const std::vector<std::string>& sources)
    {
        if (active_)
        {
            return false;
        }
        if (task_ && !task_->is_complete())
        {
            error_ = "The canceled Texture2D import is still finishing.";
            return false;
        }
        clear();
        if ((folder != "/Project" && folder.compare(0, 9, "/Project/") != 0) ||
            !VirtualPath::parse(folder).succeeded() || sources.size() > maximum_file_drop_paths)
        {
            error_ = "Select a writable Project folder and at most 32 image files.";
            return false;
        }
        folder_ = folder;
        set_sources(sources);
        active_ = true;
        open_ = true;
        browse_ = sources.empty();
        return true;
    }

    bool TextureImportDialog::request_environment(const std::string& folder, const std::vector<std::string>& sources)
    {
        if (!request(folder, sources))
        {
            return false;
        }
        environment_ = true;
        return true;
    }

    bool TextureImportDialog::request_reimport(EditorWorkspace& workspace, const AssetId& id,
                                               TextureImportSettings settings)
    {
        const auto location = workspace.catalog().index.find(id);
        if (!location || location->index.root_type != "toy3d.Texture2DAssetData" ||
            location->path.utf8().compare(0u, 9u, "/Project/") != 0)
        {
            return false;
        }
        const auto snapshot = workspace.asset_pairs().read(location->path);
        if (!snapshot.succeeded())
        {
            error_ = snapshot.status().message;
            return false;
        }
        const std::string path = location->path.utf8();
        if (!request(path.substr(0u, path.find_last_of('/'))))
        {
            return false;
        }
        reimport_id_ = id;
        reimport_path_ = path;
        reimport_baseline_ = snapshot.value().description_bytes;
        reimport_settings_ = settings;
        candidates_.front().settings = settings;
        std::string name = path.substr(path.find_last_of('/') + 1u);
        name.resize(name.size() - 6u);
        if (name.empty() || name.size() >= candidates_.front().name.size())
        {
            clear();
            error_ = "The original texture filename exceeds the supported name capacity.";
            return false;
        }
        std::memcpy(candidates_.front().name.data(), name.c_str(), name.size() + 1u);
        return true;
    }

    void TextureImportDialog::set_sources(const std::vector<std::string>& sources)
    {
        candidates_.clear();
        for (const auto& source : sources)
        {
            Candidate candidate;
            candidate.settings = reimport_id_.valid() ? reimport_settings_ : TextureImportSettings{};
            if (source.size() >= candidate.source.size() || !is_valid_utf8(source) ||
                source.find('\0') != std::string::npos)
            {
                candidate.error = "Source path is invalid or too long.";
            }
            else
            {
                std::memcpy(candidate.source.data(), source.c_str(), source.size() + 1u);
                std::string name = source.substr(source.find_last_of("/\\") + 1u);
                const auto dot = name.find_last_of('.');
                if (dot != std::string::npos)
                {
                    name.resize(dot);
                }
                for (char& c : name)
                {
                    if (static_cast<unsigned char>(c) < 32 || std::strchr("<>:\"/\\|?*", c))
                    {
                        c = '_';
                    }
                }
                if (name.size() >= candidate.name.size())
                {
                    candidate.error = "Enter a shorter resource name.";
                }
                else
                {
                    std::memcpy(candidate.name.data(), name.c_str(), name.size() + 1u);
                }
            }
            if (reimport_id_.valid())
            {
                std::string name = reimport_path_.substr(reimport_path_.find_last_of('/') + 1u);
                name.resize(name.size() - 6u);
                if (name.size() < candidate.name.size())
                {
                    std::memcpy(candidate.name.data(), name.c_str(), name.size() + 1u);
                }
            }
            candidates_.push_back(std::move(candidate));
        }
        if (candidates_.empty())
        {
            candidates_.push_back(Candidate{});
        }
    }

    void TextureImportDialog::draw(IWindow& window, EditorWorkspace& workspace, EditorSelection& selection)
    {
        if (!active_)
        {
            if (task_ && task_->is_complete())
            {
                prepared_.reset();
                task_.reset();
            }
            return;
        }
        if (browse_)
        {
            browse_ = false;
            std::vector<std::string> paths;
            if (!(environment_ ? pick_environment_files(window, paths, error_)
                               : pick_texture_files(window, paths, error_)))
            {
                TOY_LOG_ERROR("{}", error_);
            }
            else if (!paths.empty())
            {
                if (reimport_id_.valid() && paths.size() != 1u)
                {
                    error_ = "Choose one source image for reimport.";
                }
                else
                {
                    set_sources(paths);
                }
            }
            else if (open_)
            {
                clear();
                return;
            }
        }
        const char* title = environment_ ? "Environment Import" : "Texture2D Import";
        if (open_)
        {
            ImGui::OpenPopup(title);
            open_ = false;
        }
        ImGui::SetNextWindowSize(ImVec2(680, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        if (running_ && task_ && task_->is_complete())
        {
            if (task_->get_outcome() != TaskOutcome::Succeeded && prepared_->error.empty())
            {
                prepared_->error = "Texture import worker failed.";
            }
            if (prepared_->error.empty())
            {
                AssetId published;
                if (reimport_id_.valid())
                {
                    if (publish_reimported_texture_asset(workspace, reimport_path_, reimport_id_, prepared_->texture,
                                                         reimport_baseline_, prepared_->error))
                    {
                        published = reimport_id_;
                    }
                }
                else if (environment_)
                {
                    publish_environment_asset(workspace, prepared_->destination, prepared_->id, prepared_->environment,
                                              published, prepared_->error);
                }
                else
                {
                    publish_texture_asset(workspace, prepared_->destination, prepared_->id, prepared_->texture,
                                          published, prepared_->error);
                }
                prepared_->saved = published.valid();
                if (published.valid())
                {
                    selection.select_asset(published);
                }
            }
            if (!prepared_->error.empty())
            {
                TOY_LOG_ERROR("Texture2D import [{} -> {}]: {}", candidates_[next_].source.data(),
                              prepared_->destination, prepared_->error);
                candidates_[next_].error = prepared_->error;
                if (!prepared_->saved)
                {
                    failed_.push_back(std::move(candidates_[next_]));
                }
                else
                {
                    error_ += prepared_->error + "\n";
                }
            }
            prepared_.reset();
            task_.reset();
            ++next_;
        }
        if (running_ && !task_)
        {
            while (next_ < candidates_.size() && !task_)
            {
                auto& candidate = candidates_[next_];
                std::string path;
                if (!destination(candidate.source.data(), folder_, candidate.name.data(), path, candidate.error,
                                 environment_))
                {
                    TOY_LOG_ERROR("Texture2D import [{}]: {}", candidate.source.data(), candidate.error);
                    failed_.push_back(std::move(candidate));
                    ++next_;
                    continue;
                }
                if (reimport_id_.valid())
                {
                    path = reimport_path_;
                }
                AssetId id = reimport_id_;
                if (!id.valid() && (!AssetId::try_generate(id) || workspace.catalog().index.find(id)))
                {
                    candidate.error = "Could not generate a unique Texture2D asset ID.";
                    TOY_LOG_ERROR("Texture2D import [{}]: {}", candidate.source.data(), candidate.error);
                    failed_.push_back(std::move(candidate));
                    ++next_;
                    continue;
                }
                if (!TaskGraphInterface::is_running())
                {
                    candidate.error = "Texture import requires a running Task Graph.";
                    TOY_LOG_ERROR("Texture2D import [{}]: {}", candidate.source.data(), candidate.error);
                    failed_.push_back(std::move(candidate));
                    ++next_;
                    continue;
                }
                prepared_ = std::make_shared<Prepared>();
                prepared_->id = id;
                prepared_->destination = path;
                const PhysicalPath source(candidate.source.data());
                const TextureImportSettings settings = candidate.settings;
                const EnvironmentImportSettings environment_settings = candidate.environment_settings;
                const bool environment = environment_;
                auto result = prepared_;
                try
                {
                    task_ = dispatch_graph_task(
                        TaskGraphInterface::get(), "Import Texture2D",
                        [result, source, settings, environment_settings, environment](NamedThread, const GraphEventRef&)
                        {
                            try
                            {
                                if (environment)
                                {
                                    prepare_environment_asset_from_source(source, result->environment, result->error,
                                                                          environment_settings);
                                }
                                else
                                {
                                    prepare_texture_asset_from_source(source, result->texture, result->error, settings);
                                }
                            }
                            catch (const std::exception& exception)
                            {
                                result->error = exception.what();
                            }
                        });
                }
                catch (const std::exception& exception)
                {
                    candidate.error = exception.what();
                    TOY_LOG_ERROR("Texture2D import [{}]: {}", candidate.source.data(), candidate.error);
                    failed_.push_back(std::move(candidate));
                    prepared_.reset();
                    ++next_;
                }
            }
            if (next_ == candidates_.size() && !task_)
            {
                running_ = false;
                candidates_ = std::move(failed_);
                failed_.clear();
                if (candidates_.empty() && error_.empty())
                {
                    ImGui::CloseCurrentPopup();
                    clear();
                    ImGui::EndPopup();
                    return;
                }
            }
        }
        ImGui::Text("Destination: %s", folder_.c_str());
        ImGui::TextWrapped(environment_ ? "Import a 2:1 Radiance HDR panorama as a prefiltered reflection environment."
                                        : "Choose Color for color images, LinearData for packed data, or Normal for "
                                          "tangent-space normal maps.");
        ImGui::TextDisabled(environment_ ? "32 MiB source | 512 px per face | bounded offline prefilter"
                                         : "32 MiB source | 4096 px per edge | 128 MiB asset payload");
        ImGui::BeginDisabled(running_);
        if (ImGui::Button("Choose Files..."))
        {
            browse_ = true;
        }
        ImGui::BeginChild(
            "Images", ImVec2(640, std::max(85.0f, std::min(300.0f, 105.0f * static_cast<float>(candidates_.size())))),
            true);
        for (std::size_t i = 0; i < candidates_.size(); ++i)
        {
            auto& candidate = candidates_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::InputText("Source", candidate.source.data(), candidate.source.size());
            ImGui::BeginDisabled(reimport_id_.valid());
            ImGui::InputText("Resource Name", candidate.name.data(), candidate.name.size());
            ImGui::EndDisabled();
            if (environment_)
            {
                int face = 0;
                for (std::uint32_t size = 2u; size < candidate.environment_settings.face_size; size *= 2u)
                {
                    ++face;
                }
                if (ImGui::Combo("Face Resolution", &face,
                                 "2\0"
                                 "4\0"
                                 "8\0"
                                 "16\0"
                                 "32\0"
                                 "64\0"
                                 "128\0"
                                 "256\0"
                                 "512\0"))
                {
                    candidate.environment_settings.face_size = 2u << face;
                }
                int samples = static_cast<int>(candidate.environment_settings.sample_count);
                if (ImGui::SliderInt("Prefilter Samples", &samples, 16, 1024))
                {
                    candidate.environment_settings.sample_count = static_cast<std::uint32_t>(samples);
                }
            }
            else
            {
                int usage = static_cast<int>(candidate.settings.usage) - 1;
                if (ImGui::Combo("Usage", &usage, "Color\0Linear Data\0Normal\0"))
                {
                    candidate.settings.usage = static_cast<TextureUsage>(usage + 1);
                    if (candidate.settings.usage != TextureUsage::Normal)
                    {
                        candidate.settings.flip_green = false;
                    }
                }
                if (candidate.settings.usage == TextureUsage::Normal)
                {
                    ImGui::Checkbox("Flip Green Channel", &candidate.settings.flip_green);
                }
            }
            if (!candidate.error.empty())
            {
                ImGui::TextWrapped("%s", candidate.error.c_str());
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
        if (running_)
        {
            ImGui::Text("Importing %u / %u...", static_cast<unsigned>(next_ + 1u),
                        static_cast<unsigned>(candidates_.size()));
        }
        if (!error_.empty())
        {
            ImGui::TextWrapped("%s", error_.c_str());
        }
        ImGui::BeginDisabled(running_ || candidates_.empty());
        if (ImGui::Button(candidates_.size() > 1u ? "Import All" : "Import"))
        {
            error_.clear();
            failed_.clear();
            next_ = 0;
            running_ = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
            clear();
        }
        ImGui::EndPopup();
    }
} // namespace toy3d
