#include "panels/texture_import_dialog.h"

#include "asset_tools/texture_asset_tools.h"
#include "file_system/virtual_path.h"
#include "imgui.h"
#include "logging/logger.h"
#include "platform/model_file_picker.h"
#include "platform/window_interface.h"
#include "selection/editor_selection.h"
#include "text/utf8.h"
#include "task_graph/graph_task.h"
#include "task_graph/task_graph_interface.h"
#include "workspace/editor_workspace.h"

#include <algorithm>
#include <cstring>
#include <exception>

namespace toy3d
{
    namespace
    {
        bool destination(const std::string& source, const std::string& folder, const std::string& name,
            std::string& path, std::string& error)
        {
            if (folder != "/Project" && folder.compare(0, 9, "/Project/") != 0)
            { error = "Select a writable Project folder."; return false; }
            if (source.empty() || source.size() > maximum_file_drop_path_bytes || !is_valid_utf8(source) ||
                source.find('\0') != std::string::npos)
            { error = "Enter a valid UTF-8 source image path."; return false; }
            const auto dot = source.find_last_of('.');
            std::string extension = dot == std::string::npos ? "" : source.substr(dot);
            for (char& c : extension) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            if (extension != ".png" && extension != ".jpg" && extension != ".jpeg")
            { error = "Select a PNG or JPEG image."; return false; }
            if (name.empty() || name.size() >= 256u || !is_valid_utf8(name) ||
                name.find_first_of("<>:\"/\\|?*") != std::string::npos || name.back() == '.' || name.back() == ' ' ||
                std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
            { error = "Enter a valid resource name without an extension."; return false; }
            path = folder + "/" + name + ".asset";
            const auto parsed = VirtualPath::parse(path);
            if (!parsed.succeeded()) { error = parsed.status().message; return false; }
            error.clear();
            return true;
        }
    } // namespace

    void TextureImportDialog::clear()
    {
        candidates_.clear(); failed_.clear();
        // A canceled worker owns only its result, but remains the single import
        // job until completion. Its bytes must never be published afterward.
        if (!task_ || task_->is_complete()) { prepared_.reset(); task_.reset(); }
        folder_.clear(); error_.clear(); next_ = 0;
        active_ = open_ = browse_ = running_ = false;
    }

    bool TextureImportDialog::request(const std::string& folder, const std::vector<std::string>& sources)
    {
        if (active_) return false;
        if (task_ && !task_->is_complete())
        { error_ = "The canceled Texture2D import is still finishing."; return false; }
        clear();
        if ((folder != "/Project" && folder.compare(0, 9, "/Project/") != 0) ||
            !VirtualPath::parse(folder).succeeded() || sources.size() > maximum_file_drop_paths)
        { error_ = "Select a writable Project folder and at most 32 image files."; return false; }
        folder_ = folder;
        set_sources(sources);
        active_ = true;
        open_ = true;
        browse_ = sources.empty();
        return true;
    }

    void TextureImportDialog::set_sources(const std::vector<std::string>& sources)
    {
        candidates_.clear();
        for (const auto& source : sources)
        {
            Candidate candidate;
            if (source.size() >= candidate.source.size() || !is_valid_utf8(source) ||
                source.find('\0') != std::string::npos)
                candidate.error = "Source path is invalid or too long.";
            else
            {
                std::memcpy(candidate.source.data(), source.c_str(), source.size() + 1u);
                std::string name = source.substr(source.find_last_of("/\\") + 1u);
                const auto dot = name.find_last_of('.');
                if (dot != std::string::npos) name.resize(dot);
                for (char& c : name)
                    if (static_cast<unsigned char>(c) < 32 || std::strchr("<>:\"/\\|?*", c)) c = '_';
                if (name.size() >= candidate.name.size()) candidate.error = "Enter a shorter resource name.";
                else std::memcpy(candidate.name.data(), name.c_str(), name.size() + 1u);
            }
            candidates_.push_back(std::move(candidate));
        }
        if (candidates_.empty()) candidates_.push_back(Candidate{});
    }

    void TextureImportDialog::draw(IWindow& window, EditorWorkspace& workspace, EditorSelection& selection)
    {
        if (!active_)
        {
            if (task_ && task_->is_complete()) { prepared_.reset(); task_.reset(); }
            return;
        }
        if (browse_)
        {
            browse_ = false;
            std::vector<std::string> paths;
            if (!pick_texture_files(window, paths, error_)) TOY_LOG_ERROR("{}", error_);
            else if (!paths.empty()) set_sources(paths);
            else if (open_) { clear(); return; }
        }
        if (open_) { ImGui::OpenPopup("Texture2D Import"); open_ = false; }
        ImGui::SetNextWindowSize(ImVec2(680, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Texture2D Import", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        if (running_ && task_ && task_->is_complete())
        {
            if (task_->get_outcome() != TaskOutcome::Succeeded && prepared_->error.empty())
                prepared_->error = "Texture import worker failed.";
            if (prepared_->error.empty())
            {
                AssetId published;
                publish_texture_asset(workspace, prepared_->destination, prepared_->id,
                    prepared_->texture, published, prepared_->error);
                prepared_->saved = published.valid();
                if (published.valid()) selection.select_asset(published);
            }
            if (!prepared_->error.empty())
            {
                candidates_[next_].error = prepared_->error;
                if (!prepared_->saved) failed_.push_back(std::move(candidates_[next_]));
                else error_ += prepared_->error + "\n";
            }
            prepared_.reset(); task_.reset(); ++next_;
        }
        if (running_ && !task_)
        {
            while (next_ < candidates_.size() && !task_)
            {
                auto& candidate = candidates_[next_];
                std::string path;
                if (!destination(candidate.source.data(), folder_, candidate.name.data(), path, candidate.error))
                { failed_.push_back(std::move(candidate)); ++next_; continue; }
                AssetId id;
                if (!AssetId::try_generate(id) || workspace.catalog().index.find(id))
                { candidate.error = "Could not generate a unique Texture2D asset ID.";
                  failed_.push_back(std::move(candidate)); ++next_; continue; }
                if (!TaskGraphInterface::is_running())
                { candidate.error = "Texture import requires a running Task Graph.";
                  failed_.push_back(std::move(candidate)); ++next_; continue; }
                prepared_ = std::make_shared<Prepared>();
                prepared_->id = id;
                prepared_->destination = path;
                const PhysicalPath source(candidate.source.data());
                auto result = prepared_;
                try
                {
                    task_ = dispatch_graph_task(TaskGraphInterface::get(), "Import Texture2D",
                        [result, source](NamedThread, const GraphEventRef&)
                        {
                            try { prepare_texture_asset_from_source(source, result->texture, result->error); }
                            catch (const std::exception& exception) { result->error = exception.what(); }
                        });
                }
                catch (const std::exception& exception)
                { candidate.error = exception.what(); failed_.push_back(std::move(candidate));
                  prepared_.reset(); ++next_; }
            }
            if (next_ == candidates_.size() && !task_)
            {
                running_ = false;
                candidates_ = std::move(failed_);
                failed_.clear();
                if (candidates_.empty() && error_.empty()) { ImGui::CloseCurrentPopup(); clear(); ImGui::EndPopup(); return; }
            }
        }
        ImGui::Text("Destination: %s", folder_.c_str());
        ImGui::TextWrapped("PNG/JPEG to RGBA8 sRGB with a full mip chain. RGB mip filtering uses linear color; alpha stays linear.");
        ImGui::TextDisabled("32 MiB source | 4096 px per edge | 128 MiB asset payload");
        ImGui::BeginDisabled(running_);
        if (ImGui::Button("Choose Files...")) browse_ = true;
        ImGui::BeginChild("Images", ImVec2(640, std::max(85.0f, std::min(300.0f,
            105.0f * static_cast<float>(candidates_.size())))), true);
        for (std::size_t i = 0; i < candidates_.size(); ++i)
        {
            auto& candidate = candidates_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::InputText("Source", candidate.source.data(), candidate.source.size());
            ImGui::InputText("Resource Name", candidate.name.data(), candidate.name.size());
            if (!candidate.error.empty()) ImGui::TextWrapped("%s", candidate.error.c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
        if (running_) ImGui::Text("Importing %u / %u...", static_cast<unsigned>(next_ + 1u),
            static_cast<unsigned>(candidates_.size()));
        if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
        ImGui::BeginDisabled(running_ || candidates_.empty());
        if (ImGui::Button(candidates_.size() > 1u ? "Import All" : "Import"))
        {
            error_.clear(); failed_.clear(); next_ = 0; running_ = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); clear(); }
        ImGui::EndPopup();
    }
} // namespace toy3d
