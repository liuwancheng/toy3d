#include "assets/animation/skeletal_mesh_import_dialog.h"

#include <algorithm>
#include <cstring>

#include "asset/animation/animation_asset.h"
#include "asset/mesh/skeletal_mesh_asset.h"
#include "assets/mesh/static_mesh_import_dialog.h"
#include "assets/thumbnails/asset_thumbnail_pool.h"
#include "imgui.h"
#include "logging/logger.h"
#include "math/length_units.h"
#include "misc/utf8.h"
#include "platform/model_file_picker.h"
#include "scene/editor_selection.h"
#include "workspace/editor_workspace.h"
#include "assets/animation/skeletal_mesh_asset_tools.h"

namespace toy3d
{
    // --------------------------------------------------------------------------
    // SkeletalMeshImportDialog: explicit Skeleton selection and owned asynchronous import
    // --------------------------------------------------------------------------
    SkeletalMeshImportDialog::SkeletalMeshImportDialog() = default;
    SkeletalMeshImportDialog::~SkeletalMeshImportDialog() = default;

    bool SkeletalMeshImportDialog::active() const
    {
#if WITH_MODEL_IMPORT
        return active_ || (job_ && job_->busy());
#else
        return active_;
#endif
    }

    const std::string& SkeletalMeshImportDialog::error() const
    {
        return error_;
    }

    void SkeletalMeshImportDialog::clear()
    {
#if WITH_MODEL_IMPORT
        if (job_)
        {
            job_->cancel();
        }
#endif
        active_ = open_ = browse_ = false;
        source_ = {};
        name_ = {};
        folder_.clear();
        error_.clear();
        skeleton_id_ = {};
        reimport_id_ = {};
        reimport_path_.clear();
        committed_ = false;
    }

    void SkeletalMeshImportDialog::shutdown()
    {
        clear();
#if WITH_MODEL_IMPORT
        if (job_)
        {
            job_->shutdown();
        }
#endif
    }

    bool SkeletalMeshImportDialog::request(const std::string& folder, bool animation_only,
                                           const std::vector<std::string>& sources)
    {
        if (active())
        {
            error_ = "An import is already active; wait for it to finish.";
            return false;
        }
        clear();
        if ((folder != "/Project" && folder.compare(0, 9, "/Project/") != 0) ||
            !VirtualPath::parse(folder).succeeded() || sources.size() > 1)
        {
            error_ = "Select a writable Project folder and one source model per import.";
            return false;
        }
#if !WITH_MODEL_IMPORT
        error_ = "Model import is disabled in this build.";
        return false;
#else
        if (!job_)
        {
            job_ = std::make_unique<SkeletalImportJob>();
        }
        folder_ = folder;
        animation_only_ = animation_only;
        scale_ = 1.0f;
        source_unit_ = k_centimeters_per_meter;
        file_units_ = false;
        convert_units_ = true;
        reduce_influences_ = sample_at_60_ = false;
        active_ = open_ = true;
        browse_ = sources.empty();
        if (!sources.empty())
        {
            set_source(sources.front());
        }
        return true;
#endif
    }

    bool SkeletalMeshImportDialog::request_reimport(EditorWorkspace& workspace, const AssetId& id)
    {
        const auto* location = workspace.catalog().index.find(id);
        if (!location || location->path.utf8().compare(0, 9, "/Project/") != 0 ||
            (location->index.root_type != "toy3d.SkeletalMeshAssetData" &&
             location->index.root_type != "toy3d.AnimationSequenceAssetData"))
        {
            error_ = "Reimport requires a writable SkeletalMesh or AnimationSequence asset.";
            return false;
        }
        const auto path = location->path.utf8();
        const bool animation = location->index.root_type == "toy3d.AnimationSequenceAssetData";
        if (!request(path.substr(0, path.find_last_of('/')), animation))
        {
            return false;
        }
        const auto pair = workspace.asset_pairs().read(location->path);
        if (!pair.succeeded())
        {
            clear();
            error_ = pair.status().message;
            return false;
        }
        AssetRef reference;
        if (animation)
        {
            const auto data = decode_animation_sequence_asset_pair(pair.value());
            if (!data.succeeded())
            {
                clear();
                error_ = data.status().message;
                return false;
            }
            reference = data.value().data.skeleton;
        }
        else
        {
            const auto data = decode_skeletal_mesh_asset_pair(pair.value());
            if (!data.succeeded())
            {
                clear();
                error_ = data.status().message;
                return false;
            }
            reference = data.value().data.skeleton;
        }
        reimport_id_ = id;
        reimport_path_ = path;
        skeleton_id_ = reference.asset_id;
        return true;
    }

    void SkeletalMeshImportDialog::set_source(const std::string& source)
    {
        source_ = {};
        name_ = {};
        if (source.size() >= source_.size() || source.find('\0') != std::string::npos || !is_valid_utf8(source))
        {
            error_ = "Choose a valid UTF-8 source path up to 4096 bytes.";
            return;
        }
        std::memcpy(source_.data(), source.c_str(), source.size() + 1);
        std::string name = source.substr(source.find_last_of("/\\") + 1);
        const auto dot = name.find_last_of('.');
        std::string extension = dot == std::string::npos ? "" : name.substr(dot);
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       {
                           return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
                       });
        file_units_ = extension == ".fbx";
        source_unit_ = file_units_ ? 1.0f : k_centimeters_per_meter;
        if (dot != std::string::npos)
        {
            name.resize(dot);
        }
        for (auto& c : name)
        {
            if (static_cast<unsigned char>(c) < 32 || std::strchr("<>:\"/\\|?*", c))
            {
                c = '_';
            }
        }
        if (name.size() >= name_.size())
        {
            error_ = "Enter a shorter resource name.";
        }
        else
        {
            std::memcpy(name_.data(), name.c_str(), name.size() + 1);
        }
    }

    void SkeletalMeshImportDialog::draw(IWindow& window, EditorWorkspace& workspace, EditorSelection& selection,
                                        AssetThumbnailPool& thumbnails)
    {
#if WITH_MODEL_IMPORT
        if (job_ && job_->update(workspace) && active_)
        {
            error_ = job_->error();
            committed_ = !job_->committed().empty();
            for (const auto& warning : job_->warnings())
            {
                TOY_LOG_WARN("Skeletal import: {}", warning);
                error_ += warning + "\n";
            }
            for (const auto& id : job_->committed())
            {
                const auto* location = workspace.catalog().index.find(id);
                if (location)
                {
                    error_ += "Saved: " + location->path.utf8() + "\n";
                }
                else
                {
                    error_ += "Saved asset ID: " + id.hex() + "\n";
                }
                selection.select_asset(id);
            }
            if (committed_)
            {
                thumbnails.invalidate();
            }
            if (!job_->error().empty())
            {
                TOY_LOG_ERROR("Skeletal import: {}", job_->error());
            }
        }
#endif
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
            else if (paths.size() > 1)
            {
                error_ = "Choose one FBX/glTF/GLB source per skeletal import.";
            }
            else if (!paths.empty())
            {
                set_source(paths.front());
            }
            else if (open_)
            {
                clear();
                return;
            }
        }
        const char* title = animation_only_ ? "Animation Import" : "Skeletal Mesh Import";
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
#if WITH_MODEL_IMPORT
        const bool running = job_ && job_->busy();
#else
        const bool running = false;
#endif
        ImGui::Text("Destination: %s", reimport_id_.valid() ? reimport_path_.c_str() : folder_.c_str());
        ImGui::TextWrapped(
            animation_only_
                ? "Import one clip using an existing Skeleton. The source must include a compatible preview mesh."
                : "Import a skinned mesh and all its clips. Create a Skeleton or reuse a compatible existing one.");
        ImGui::TextWrapped(reimport_id_.valid() ? "Reimport preserves this asset ID and replaces only this asset; "
                                                  "Skeleton and other clips are retained."
                                                : "Source materials and textures are not imported. Each asset is saved "
                                                  "separately; committed files are retained on failure.");
        ImGui::BeginDisabled(running || committed_);
        if (ImGui::Button("Choose Source..."))
        {
            browse_ = true;
        }
        ImGui::SetNextItemWidth(520);
        if (ImGui::InputText("Source", source_.data(), source_.size()))
        {
            const std::string path(source_.data());
            set_source(path);
        }
        if (!reimport_id_.valid())
        {
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("Resource Name", name_.data(), name_.size());
        }
        const auto* selected = workspace.catalog().index.find(skeleton_id_);
        ImGui::BeginDisabled(reimport_id_.valid());
        if (ImGui::BeginCombo("Skeleton", selected          ? selected->path.utf8().c_str()
                                          : animation_only_ ? "Select an existing Skeleton"
                                                            : "Create new Skeleton"))
        {
            if (!animation_only_ && ImGui::Selectable("Create new Skeleton", !skeleton_id_.valid()))
            {
                skeleton_id_ = {};
            }
            for (const auto& entry : workspace.catalog().entries)
            {
                if (entry.file.root_type == "toy3d.SkeletonAssetData" &&
                    ImGui::Selectable(entry.path.utf8().c_str(), entry.file.asset_id == skeleton_id_))
                {
                    skeleton_id_ = entry.file.asset_id;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::Checkbox("Convert Scene Unit", &convert_units_);
        ImGui::Checkbox("Use FBX File Units", &file_units_);
        ImGui::BeginDisabled(!convert_units_ || file_units_);
        ImGui::InputFloat("Source Unit (cm)", &source_unit_);
        ImGui::EndDisabled();
        ImGui::InputFloat("Import Uniform Scale", &scale_);
        ImGui::Checkbox("Sample at 60 Hz (default 30 Hz)", &sample_at_60_);
        ImGui::Checkbox("Allow reducing more than 8 influences", &reduce_influences_);
        ImGui::EndDisabled();
        if (running)
        {
            ImGui::TextUnformatted("Parsing and building assets in the background...");
        }
        if (!error_.empty())
        {
            ImGui::BeginChild("Import Results", ImVec2(0, 140), true);
            ImGui::TextWrapped("%s", error_.c_str());
            ImGui::EndChild();
        }
#if WITH_MODEL_IMPORT
        ImGui::BeginDisabled(running || committed_);
        if (ImGui::Button(reimport_id_.valid() ? "Reimport" : "Import", ImVec2(120, 0)))
        {
            error_.clear();
            std::string destination;
            std::string name = name_.data();
            if (reimport_id_.valid())
            {
                name = reimport_path_.substr(reimport_path_.find_last_of('/') + 1);
                name.resize(name.size() - std::string(".asset").size());
            }
            SkeletalMeshImportOptions options;
            options.coordinates.import_uniform_scale = scale_;
            options.coordinates.convert_scene_unit = convert_units_;
            options.coordinates.use_file_unit = file_units_;
            options.coordinates.source_unit_in_centimeters = source_unit_;
            options.sample_rate = sample_at_60_ ? 60 : 30;
            options.skin.allow_reduce_influences = reduce_influences_;
            SkeletalImportRequest request;
            if (static_mesh_import_destination(source_.data(), folder_, name, scale_, destination, error_) &&
                capture_skeletal_import(workspace, PhysicalPath(source_.data()), destination,
                                        animation_only_ ? SkeletalImportMode::AnimationOnly
                                                        : SkeletalImportMode::MeshAndAnimations,
                                        skeleton_id_, options, reimport_id_, request, error_))
            {
                job_->start(std::move(request), error_);
            }
        }
        ImGui::EndDisabled();
#endif
        ImGui::SameLine();
        if (ImGui::Button(committed_ ? "Close" : "Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
            clear();
        }
        ImGui::EndPopup();
    }
} // namespace toy3d
