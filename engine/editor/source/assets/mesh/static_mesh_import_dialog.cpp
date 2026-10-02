#include "assets/mesh/static_mesh_import_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "imgui.h"
#include "file_system/virtual_path.h"
#include "logging/logger.h"
#include "platform/model_file_picker.h"
#include "platform/window_interface.h"
#include "scene/editor_selection.h"
#include "misc/utf8.h"
#include "assets/thumbnails/asset_thumbnail_pool.h"
#include "workspace/editor_workspace.h"
#if WITH_MODEL_IMPORT
#include "asset_pipeline/static_mesh_import.h"
#include "assets/mesh/static_mesh_asset_tools.h"
#endif

namespace toy3d
{
    bool static_mesh_import_destination(const std::string& source, const std::string& folder, const std::string& name,
                                        float scale, std::string& destination, std::string& error)
    {
        error.clear();
        if (folder != "/Project" && folder.compare(0, 9, "/Project/") != 0)
        {
            error = "Import into a writable Project folder. Engine content is read only.";
            return false;
        }
        if (source.empty() || source.size() > maximum_file_drop_path_bytes || source.find('\0') != std::string::npos ||
            !is_valid_utf8(source))
        {
            error = "Enter a valid UTF-8 source file path (up to 4096 bytes).";
            return false;
        }
        const auto separator = source.find_last_of("/\\");
        const auto dot = source.find_last_of('.');
        std::string extension =
            dot != std::string::npos && (separator == std::string::npos || dot > separator) ? source.substr(dot) : "";
        for (char& c : extension)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c + ('a' - 'A'));
            }
        }
        if (extension != ".fbx" && extension != ".obj" && extension != ".gltf" && extension != ".glb")
        {
            error = "Select an FBX, OBJ, glTF or GLB model file.";
            return false;
        }
        if (name.empty() || name.size() >= 256 || !is_valid_utf8(name) ||
            name.find_first_of("<>:\"/\\|?*") != std::string::npos || name.back() == '.' || name.back() == ' ' ||
            std::any_of(name.begin(), name.end(),
                        [](unsigned char c)
                        {
                            return c < 32;
                        }))
        {
            error = "Enter a resource name without extension, directories or reserved filename characters.";
            return false;
        }
        if (!std::isfinite(scale) || scale <= 0)
        {
            error = "Scale multiplier must be finite and greater than zero.";
            return false;
        }
        const std::string candidate = folder + "/" + name + ".asset";
        const auto parsed = VirtualPath::parse(candidate);
        if (!parsed.succeeded())
        {
            error = parsed.status().message;
            return false;
        }
        destination = candidate;
        return true;
    }

    // --------------------------------------------------------------------------
    // StaticMeshImportDialog: per-file unit policy and batch import interaction
    // --------------------------------------------------------------------------
    void StaticMeshImportDialog::clear()
    {
        candidates_.clear();
        folder_.clear();
        error_.clear();
        active_ = false;
        open_ = false;
        browse_ = false;
    }

    bool StaticMeshImportDialog::request(const std::string& folder, const std::vector<std::string>& sources)
    {
        if (active_)
        {
            return false;
        }
        clear();
        if ((folder != "/Project" && folder.compare(0, 9, "/Project/") != 0) || !VirtualPath::parse(folder).succeeded())
        {
            error_ = "Select a writable Project folder before importing.";
            return false;
        }
        if (sources.size() > maximum_file_drop_paths)
        {
            error_ = "Import at most 32 model files at a time.";
            return false;
        }
        folder_ = folder;
        scale_ = 1;
        convert_scene_unit_ = true;
        set_sources(sources);
        active_ = true;
        open_ = true;
        browse_ = sources.empty();
        return true;
    }

    void StaticMeshImportDialog::set_sources(const std::vector<std::string>& sources)
    {
        candidates_.clear();
        for (const std::string& source : sources)
        {
            Candidate candidate;
            if (source.size() >= candidate.source.size() || source.find('\0') != std::string::npos ||
                !is_valid_utf8(source))
            {
                candidate.error = "Source path is invalid or too long; choose another file.";
            }
            else
            {
                std::memcpy(candidate.source.data(), source.c_str(), source.size() + 1);
                std::string name = source.substr(source.find_last_of("/\\") + 1);
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
                    candidate.error = "Source name is too long; enter a shorter resource name.";
                }
                else
                {
                    std::memcpy(candidate.name.data(), name.c_str(), name.size() + 1);
                }
            }
            suggest_source_unit(candidate);
            candidates_.push_back(std::move(candidate));
        }
        if (candidates_.empty())
        {
            candidates_.push_back(Candidate{});
        }
    }

    void StaticMeshImportDialog::suggest_source_unit(Candidate& candidate)
    {
        std::string source(candidate.source.data());
        const auto dot = source.find_last_of('.');
        std::string extension = dot == std::string::npos ? "" : source.substr(dot);
        for (char& c : extension)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c + ('a' - 'A'));
            }
        }
        // Format suggestions are editable UI policy, never importer assumptions.
        candidate.is_fbx = extension == ".fbx";
        candidate.use_file_unit = candidate.is_fbx;
        candidate.source_unit_in_centimeters = candidate.use_file_unit ? 1.0f : k_centimeters_per_meter;
    }

    void StaticMeshImportDialog::draw(IWindow& window, EditorWorkspace& workspace, EditorSelection& selection,
                                      AssetThumbnailPool& thumbnails)
    {
        if (!active_)
        {
            return;
        }
        if (browse_)
        {
            browse_ = false;
            std::vector<std::string> paths;
            if (!pick_model_files(window, paths, error_))
            {
                TOY_LOG_ERROR("{}", error_);
            }
            else if (!paths.empty())
            {
                set_sources(paths);
            }
            else if (open_)
            {
                clear();
                return;
            } // Initial picker cancellation.
        }
        if (open_)
        {
            ImGui::OpenPopup("Static Mesh Import");
            open_ = false;
        }
        const ImVec2 display = ImGui::GetMainViewport()->WorkSize;
        const float dialog_width = std::max(320.0f, std::min(720.0f, display.x - 32));
        ImGui::SetNextWindowSize(ImVec2(dialog_width, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Static Mesh Import", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextUnformatted("STATIC MESH IMPORT");
        ImGui::Text("Destination: %s", folder_.c_str());
        ImGui::TextWrapped(
            "Static meshes in each source file are combined. Source transforms and origins are preserved.");
        ImGui::TextWrapped("Default material; source material slots are retained. Animation, source lights/cameras and "
                           "collision are not imported.");
        ImGui::Separator();
        if (ImGui::Button("Choose Files..."))
        {
            browse_ = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("FBX / OBJ / glTF / GLB | up to 32 files");
        ImGui::Checkbox("Convert Scene Unit", &convert_scene_unit_);
        ImGui::SetNextItemWidth(160);
        ImGui::InputFloat("Import Uniform Scale", &scale_);
        ImGui::TextWrapped("World units: centimeters. Source Unit = cm per source unit (cm: 1, m: 100, mm: 0.1). OBJ "
                           "defaults to meters; override it to match your file.");
        const float height = std::max(
            85.0f, std::min(std::min(310.0f, display.y - 370), static_cast<float>(candidates_.size()) * 170.0f));
        ImGui::BeginChild("Import Candidates", ImVec2(0, height), true);
        for (std::size_t i = 0; i < candidates_.size(); ++i)
        {
            Candidate& candidate = candidates_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(std::max(150.0f, ImGui::GetContentRegionAvail().x - 120));
            if (ImGui::InputText("Source", candidate.source.data(), candidate.source.size()))
            {
                suggest_source_unit(candidate);
            }
            ImGui::SetNextItemWidth(std::max(150.0f, ImGui::GetContentRegionAvail().x - 120));
            ImGui::InputText("Resource Name", candidate.name.data(), candidate.name.size());
            ImGui::BeginDisabled(!convert_scene_unit_);
            ImGui::BeginDisabled(!candidate.is_fbx);
            ImGui::Checkbox("Use FBX File Units", &candidate.use_file_unit);
            ImGui::EndDisabled();
            ImGui::BeginDisabled(candidate.use_file_unit);
            ImGui::SetNextItemWidth(160);
            ImGui::InputFloat("Source Unit (cm)", &candidate.source_unit_in_centimeters);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (!candidate.error.empty())
            {
                ImGui::TextWrapped("%s", candidate.error.c_str());
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (!error_.empty())
        {
            ImGui::TextWrapped("%s", error_.c_str());
        }
#if WITH_MODEL_IMPORT
        ImGui::BeginDisabled(candidates_.empty());
        if (ImGui::Button(candidates_.size() > 1 ? "Import All" : "Import", ImVec2(120, 0)))
        {
            error_.clear();
            std::vector<Candidate> failed;
            for (Candidate& candidate : candidates_)
            {
                std::string destination;
                AssetId id;
                StaticMeshImportOptions options;
                options.import_uniform_scale = scale_;
                options.convert_scene_unit = convert_scene_unit_;
                options.use_file_unit = candidate.use_file_unit;
                options.source_unit_in_centimeters = candidate.source_unit_in_centimeters;
                if (!static_mesh_import_destination(candidate.source.data(), folder_, candidate.name.data(), scale_,
                                                    destination, candidate.error) ||
                    !import_static_mesh_to_workspace(workspace, PhysicalPath(candidate.source.data()), destination,
                                                     options, id, candidate.error))
                {
                    TOY_LOG_ERROR("Model import failed: {}", candidate.error);
                    // Publishing may succeed before catalog refresh fails.
                    // Do not present an already published file as retryable.
                    if (id.valid())
                    {
                        error_ += candidate.error + "\n";
                    }
                    else
                    {
                        failed.push_back(std::move(candidate));
                    }
                }
                else
                {
                    selection.select_asset(id);
                    thumbnails.generate(id);
                }
            }
            candidates_ = std::move(failed);
            if (candidates_.empty() && error_.empty())
            {
                ImGui::CloseCurrentPopup();
                clear();
            }
        }
        ImGui::EndDisabled();
#else
        ImGui::TextDisabled("Model import is disabled in this build.");
#endif
        ImGui::SameLine();
        if (ImGui::Button(candidates_.empty() ? "Close" : "Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
            clear();
        }
        ImGui::EndPopup();
    }
} // namespace toy3d
