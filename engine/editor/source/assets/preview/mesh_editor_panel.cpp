#include "assets/preview/mesh_editor_panel.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <limits>
#include <utility>

#include "imgui.h"
#include "asset/texture/builtin_texture_assets.h"
#include "assets/asset_resource_picker.h"
#include "assets/mesh/mesh_material_slots.h"
#include "assets/preview/preview_scene_widgets.h"
#include "gamescene/component/skeletal_mesh_component.h"
#include "drivers/rhi/rhi_command_descriptors.h"
#include "logging/logger.h"
#include "panels/property_widgets.h"
#include "rendercore/texture/texture_asset_loader.h"
#include "threading/task_graph/graph_task.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        std::string name_for(const AssetCatalog& catalog, const AssetId& id, const char* fallback)
        {
            const auto* location = catalog.index.find(id);
            if (!location)
            {
                return fallback;
            }
            const auto& path = location->path.utf8();
            return path.substr(path.find_last_of('/') + 1);
        }
    } // namespace

    // --------------------------------------------------------------------------
    // CpuResult: immutable worker candidate with a revocable session revision
    // --------------------------------------------------------------------------
    struct MeshEditorPanel::CpuResult
    {
        std::uint64_t revision = 0;
        std::shared_ptr<const MeshPreviewAsset> asset;
        std::string error;
    };

    // --------------------------------------------------------------------------
    // MeshEditorPanel: static mesh and associated skeleton/sequence preview
    // --------------------------------------------------------------------------
    MeshEditorPanel::MeshEditorPanel(EditorWorkspace& workspace) : workspace_(workspace), material_session_(workspace)
    {
        playback_.autoplay = false;
    }

    MeshEditorPanel::~MeshEditorPanel()
    {
        shutdown();
    }

    bool MeshEditorPanel::initialize(SceneInterface& scene, MaterialInstanceRef material, TaskGraphInterface& tasks)
    {
        SceneEnvironmentSettings environment;
        environment.environment.asset_id = preview_settings_.environment;
        environment.environment.expected_type = "toy3d.EnvironmentAssetData";
        const auto cube =
            load_environment_asset(workspace_.files(), workspace_.catalog().index, environment.environment);
        if (!cube.succeeded())
        {
            error_ = cube.status().message;
            return false;
        }
        material_ = std::move(material);
        tasks_ = &tasks;
        environment_cube_ = cube.value();
        loaded_environment_ = preview_settings_.environment;
        initialized_ = scene_.initialize(scene, material_, environment, environment_cube_);
        if (!initialized_)
        {
            scene_.shutdown();
        }
        return initialized_;
    }

    void MeshEditorPanel::request_open(const AssetId& id, bool focus)
    {
        if (!id.valid())
        {
            return;
        }
        if (material_session_.dirty() && !(requested_id_ == id))
        {
            pending_open_id_ = id;
            return;
        }
        open_ = visible_ = true;
        focus_requested_ = focus;
        if (requested_id_ == id && (asset_ || cpu_task_ || needs_load_))
        {
            return;
        }
        requested_id_ = id;
        mesh_preference_pending_ = false;
        override_selection_ = false;
        ++revision_;
        needs_load_ = true;
        error_.clear();
    }

    void MeshEditorPanel::invalidate()
    {
        loaded_environment_ = {};
        cached_asset_.reset();
        cached_mesh_.reset();
        if (material_session_.dirty())
        {
            render_dirty_ = true;
            return;
        }
        if (open_ && requested_id_.valid())
        {
            ++revision_;
            needs_load_ = true;
        }
    }

    void MeshEditorPanel::select(const AssetId& mesh, const AssetId& sequence)
    {
        if (material_session_.dirty())
        {
            error_ = "Save or discard mesh material changes before changing the preview selection.";
            return;
        }
        if (asset_ && asset_->root_type == "toy3d.AnimationSequenceAssetData" && !(selected_mesh_ == mesh))
        {
            mesh_preference_pending_ = true;
        }
        selected_mesh_ = mesh;
        selected_sequence_ = sequence;
        override_selection_ = true;
        ++revision_;
        needs_load_ = true;
        error_.clear();
    }

    bool MeshEditorPanel::adopt(std::shared_ptr<const MeshPreviewAsset> candidate)
    {
        AnimationInstance animation;
        std::vector<AnimationSequenceInput> sources;
        if (candidate->sequence)
        {
            sources.push_back({candidate->sequence, playback_});
        }
        if (candidate->layout)
        {
            const auto status = animation.set_sources(candidate->layout, sources);
            if (!status.succeeded())
            {
                error_ = status.message;
                return false;
            }
        }
        const auto slots = candidate->static_mesh ? candidate->static_mesh->material_slots
                           : candidate->mesh      ? candidate->mesh->data.material_slots
                                                  : std::vector<std::string>{};
        const auto references = candidate->static_mesh ? candidate->static_mesh->default_materials
                                : candidate->mesh      ? candidate->mesh->data.default_materials
                                                       : std::vector<AssetRef>{};
        auto materials = load_mesh_materials(
            slots, references, material_, material_resolver_,
            candidate->static_mesh ? shader::VertexFactoryType::Local : shader::VertexFactoryType::GPUSkin,
            candidate->static_mesh ? candidate->static_mesh->valid_tangent_frame
                                   : candidate->mesh && candidate->mesh->geometry.mesh.valid_tangent_frame);
        if (!materials.succeeded())
        {
            error_ = materials.status().message;
            return false;
        }
        SkeletalMeshRef mesh;
        if (candidate->mesh && cached_asset_ && candidate->mesh == cached_asset_->mesh &&
            candidate->layout == cached_asset_->layout)
        {
            mesh = cached_mesh_;
        }
        if (candidate->mesh && !mesh)
        {
            const auto created = SkeletalMesh::create(candidate->layout, *candidate->mesh, materials.value());
            if (!created.succeeded())
            {
                error_ = created.status().message;
                return false;
            }
            mesh = created.value();
        }
        asset_ = std::move(candidate);
        preview_materials_ = std::move(materials).value();
        material_session_.clear();
        if (asset_->static_mesh || asset_->root_type == "toy3d.SkeletalMeshAssetData")
        {
            const auto opened = material_session_.open(asset_->id,
                                                       [this](const std::vector<AssetRef>& values)
                                                       {
                                                           return prepare_materials(values);
                                                       });
            if (!opened.succeeded())
            {
                error_ = opened.message;
            }
        }
        mesh_ = std::move(mesh);
        animation_ = std::move(animation);
        selected_mesh_ = asset_->mesh_id;
        selected_sequence_ = asset_->sequence_id;
        selected_bone_ = -1;
        mesh_dirty_ = render_dirty_ = true;
        compatible_sequences_.clear();
        for (const auto& entry : workspace_.catalog().entries)
        {
            if (asset_->layout && entry.file.root_type == "toy3d.AnimationSequenceAssetData" &&
                animation_asset_matches_layout(workspace_.types(), workspace_.files(), entry, *asset_->layout))
            {
                compatible_sequences_.push_back(entry.file.asset_id);
            }
        }
        frame_all();
        return true;
    }

    bool MeshEditorPanel::prepare_mesh()
    {
        if (asset_->static_mesh)
        {
            // Visibility leaves the studio framing intact so hiding a large mesh
            // does not also shrink or move its floor and shadow range.
            auto geometry = *asset_->static_mesh;
            if (material_session_.active())
            {
                geometry.default_materials = material_session_.materials();
            }
            if (!scene_.prepare_static(geometry) || !scene_.set_mesh_visible(show_mesh_))
            {
                error_ = "Could not register the static preview mesh.";
                return false;
            }
        }
        else if (show_mesh_ && mesh_)
        {
            if (material_session_.active())
            {
                auto data = *asset_->mesh;
                data.data.default_materials = material_session_.materials();
                const auto prepared = SkeletalMesh::create(asset_->layout, std::move(data), preview_materials_);
                if (!prepared.succeeded())
                {
                    error_ = prepared.status().message;
                    return false;
                }
                mesh_ = prepared.value();
            }
            if (!scene_.prepare_skeletal(mesh_, asset_->sequence))
            {
                error_ = "Could not register the skeletal preview mesh.";
                return false;
            }
        }
        else
        {
            scene_.clear_mesh();
        }
        mesh_dirty_ = false;
        return true;
    }

    void MeshEditorPanel::tick(double delta_seconds)
    {
        if (!initialized_)
        {
            return;
        }
        if (cpu_task_)
        {
            loading_seconds_ += delta_seconds;
        }
        if (cpu_task_ && cpu_task_->is_complete())
        {
            auto result = std::move(cpu_result_);
            if (cpu_task_->get_outcome() != TaskOutcome::Succeeded && result->error.empty())
            {
                result->error = "Mesh preview worker failed.";
            }
            cpu_task_.reset();
            if (open_ && result->revision == revision_)
            {
                if (result->error.empty() && !mesh_preview_asset_current(workspace_.asset_pairs(), workspace_.catalog(),
                                                                         *result->asset, &workspace_.files()))
                {
                    result->error = "Preview inputs changed during load; rescan and reopen.";
                }
                if (!result->error.empty())
                {
                    mesh_preference_pending_ = false;
                    error_ = std::move(result->error);
                    requested_id_ = asset_ ? asset_->id : AssetId();
                    selected_mesh_ = asset_ ? asset_->mesh_id : AssetId();
                    selected_sequence_ = asset_ ? asset_->sequence_id : AssetId();
                }
                else
                {
                    auto previous = asset_;
                    if (adopt(result->asset))
                    {
                        previous_asset_ = std::move(previous);
                    }
                }
            }
        }
        if (open_ && needs_load_ && !cpu_task_ && !candidate_id_.valid())
        {
            auto result = std::make_shared<CpuResult>();
            loading_seconds_ = 0;
            result->revision = revision_;
            cpu_result_ = result;
            AssetPairStore* pairs = &workspace_.asset_pairs();
            const FileSystem* files = &workspace_.files();
            const auto catalog = workspace_.catalog();
            const auto id = requested_id_;
            const auto mesh = selected_mesh_;
            const auto sequence = selected_sequence_;
            const bool override_selection = override_selection_;
            const auto reusable = cached_asset_;
            try
            {
                cpu_task_ = dispatch_graph_task(
                    *tasks_, "Load mesh preview",
                    [result, pairs, files, catalog, id, mesh, sequence, override_selection,
                     reusable](NamedThread, const GraphEventRef&)
                    {
                        const auto started = std::chrono::steady_clock::now();
                        try
                        {
                            auto loaded = load_mesh_preview_asset(*pairs, catalog, id, override_selection, mesh,
                                                                  sequence, reusable, files);
                            if (loaded.succeeded())
                            {
                                result->asset = std::make_shared<const MeshPreviewAsset>(std::move(loaded).value());
                            }
                            else
                            {
                                result->error = loaded.status().message;
                            }
                        }
                        catch (const std::exception& error)
                        {
                            result->error = error.what();
                        }
                        const auto* location = catalog.index.find(id);
                        TOY_LOG_INFO("Mesh preview load [{}] {} in {} ms{}{}",
                                     location ? location->path.utf8() : "unknown asset",
                                     result->error.empty() ? "completed" : "failed",
                                     std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - started)
                                         .count(),
                                     result->error.empty() ? "" : ": ", result->error);
                    });
            }
            catch (const std::exception& error)
            {
                error_ = error.what();
                cpu_result_.reset();
            }
            needs_load_ = false;
        }
        if (open_ && visible_ && asset_ && asset_->layout && !needs_load_ && !cpu_task_)
        {
            AnimationUpdateInput update;
            update.delta_time = delta_seconds;
            update.weights = asset_->sequence ? std::vector<double>{1} : std::vector<double>{};
            update.lock_root = lock_root_;
            const auto status = animation_.update(update);
            if (!status.succeeded())
            {
                error_ = status.message;
            }
            const auto* clock = animation_.playback_state(0);
            render_dirty_ = render_dirty_ || (clock && clock->playing());
        }
    }

    void MeshEditorPanel::frame_all()
    {
        if (!asset_)
        {
            return;
        }
        if (asset_->static_mesh)
        {
            Vector3 minimum = asset_->static_mesh->vertices.front().position;
            Vector3 maximum = minimum;
            for (const auto& vertex : asset_->static_mesh->vertices)
            {
                minimum.x = std::min(minimum.x, vertex.position.x);
                minimum.y = std::min(minimum.y, vertex.position.y);
                minimum.z = std::min(minimum.z, vertex.position.z);
                maximum.x = std::max(maximum.x, vertex.position.x);
                maximum.y = std::max(maximum.y, vertex.position.y);
                maximum.z = std::max(maximum.z, vertex.position.z);
            }
            center_ = (minimum + maximum) * 0.5f;
            radius_ = std::max(1.0f, length(maximum - minimum) * 0.5f);
            distance_ = radius_ * 3.5f;
            render_dirty_ = true;
            return;
        }
        AnimationUpdateInput update;
        update.weights = asset_->sequence ? std::vector<double>{1} : std::vector<double>{};
        update.lock_root = lock_root_;
        const auto updated = animation_.update(update);
        if (!updated.succeeded())
        {
            error_ = updated.message;
            return;
        }
        const auto evaluated = animation_.evaluate();
        if (!evaluated.succeeded() || evaluated.value()->component_pose.bone_matrices.empty())
        {
            return;
        }
        const auto& matrices = evaluated.value()->component_pose.bone_matrices;
        Vector3 minimum = transform_position(matrices.front(), Vector3());
        Vector3 maximum = minimum;
        auto include = [&minimum, &maximum](const Vector3& point)
        {
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
        };
        for (const auto& matrix : matrices)
        {
            include(transform_position(matrix, Vector3()));
        }
        if (mesh_)
        {
            SkeletalMeshDeformer deformer;
            const auto bound = deformer.set_mesh(mesh_);
            if (!bound.succeeded())
            {
                error_ = bound.message;
                return;
            }
            const auto deformed = deformer.evaluate(*evaluated.value());
            if (!deformed.succeeded())
            {
                error_ = deformed.status().message;
                return;
            }
            include(deformed.value().bounds_minimum);
            include(deformed.value().bounds_maximum);
        }
        center_ = (minimum + maximum) * 0.5f;
        radius_ = std::max(1.0f, length(maximum - minimum) * 0.5f);
        distance_ = radius_ * 3.5f;
        render_dirty_ = true;
    }

    SceneView MeshEditorPanel::view() const
    {
        const float yaw = yaw_ * k_pi / 180;
        const float pitch = pitch_ * k_pi / 180;
        const Vector3 position =
            center_ +
            Vector3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)) * distance_;
        Vector3 forward;
        Quaternion rotation;
        try_normalize(center_ - position, forward);
        try_make_rotation_from_forward_up(forward, Vector3(0, 1, 0), rotation);
        return SceneView(position, rotation, forward, {0, 0, extent_.width, extent_.height}, extent_,
                         CameraProjectionMode::Perspective, Radians(k_pi / 4), std::max(0.01f, radius_ * 0.005f),
                         std::max(1000.0f, distance_ + radius_ * 50));
    }

    std::vector<DebugLineVertex> MeshEditorPanel::bone_lines(const AnimationEvaluation& evaluation) const
    {
        std::vector<DebugLineVertex> lines;
        const auto& bones = asset_->layout->skeleton().bones;
        const auto& matrices = evaluation.component_pose.bone_matrices;
        const float joint_size = std::max(0.05f, radius_ * 0.006f);
        for (std::size_t i = 0; i < bones.size(); ++i)
        {
            const Vector3 position = transform_position(matrices[i], Vector3());
            const Vector4 color = selected_bone_ == static_cast<std::int32_t>(i) ? Vector4(5, 1.5f, 0.1f, 1)
                                                                                 : Vector4(0.1f, 2.0f, 2.5f, 1);
            if (bones[i].parent_index >= 0)
            {
                lines.push_back({Vector4(transform_position(matrices[bones[i].parent_index], Vector3()), 1), color});
                lines.push_back({Vector4(position, 1), color});
            }
            for (const auto& axis : {Vector3(joint_size, 0, 0), Vector3(0, joint_size, 0), Vector3(0, 0, joint_size)})
            {
                lines.push_back({Vector4(position - axis, 1), color});
                lines.push_back({Vector4(position + axis, 1), color});
            }
        }
        return lines;
    }

    void MeshEditorPanel::collect_render_work(UiRenderWork& work)
    {
        work.retire_textures.insert(work.retire_textures.end(), pending_work_.retire_textures.begin(),
                                    pending_work_.retire_textures.end());
        work.release_animation_preview = work.release_animation_preview || pending_work_.release_animation_preview;
        pending_work_ = {};
        // Do not mutate the scene while RT may still retry a request using its old pose.
        if (!initialized_ || !open_ || !visible_ || !asset_ || needs_load_ || cpu_task_ || candidate_id_.valid() ||
            !render_dirty_)
        {
            return;
        }
        // Resolve a full environment candidate before changing the mesh or World.
        TextureRef environment_cube = environment_cube_;
        if (!(loaded_environment_ == preview_settings_.environment))
        {
            environment_cube.reset();
            if (preview_settings_.environment.valid())
            {
                AssetRef reference;
                reference.asset_id = preview_settings_.environment;
                reference.expected_type = "toy3d.EnvironmentAssetData";
                const auto loaded = load_environment_asset(workspace_.files(), workspace_.catalog().index, reference);
                if (!loaded.succeeded())
                {
                    error_ = "Preview environment: " + loaded.status().message;
                    render_dirty_ = false;
                    return;
                }
                environment_cube = loaded.value();
            }
        }
        if (mesh_dirty_ && !prepare_mesh())
        {
            render_dirty_ = false;
            return;
        }
        if (!scene_.configure(preview_settings_, environment_cube))
        {
            error_ = "Could not configure the mesh preview scene.";
            render_dirty_ = false;
            return;
        }
        loaded_environment_ = preview_settings_.environment;
        environment_cube_ = std::move(environment_cube);
        if (asset_->layout)
        {
            AnimationUpdateInput update;
            update.weights = asset_->sequence ? std::vector<double>{1} : std::vector<double>{};
            update.lock_root = lock_root_;
            const auto updated = animation_.update(update);
            if (!updated.succeeded())
            {
                error_ = updated.message;
                render_dirty_ = false;
                return;
            }
            if (auto* component = scene_.skeletal_component())
            {
                auto settings = playback_;
                settings.autoplay = false;
                auto status = component->set_playback_settings(settings, lock_root_);
                const auto* clock = animation_.playback_state(0);
                if (status.succeeded() && clock)
                {
                    status = component->seek(clock->time());
                }
                if (!status.succeeded())
                {
                    error_ = status.message;
                    return;
                }
            }
        }
        std::vector<DebugLineVertex> debug_lines;
        if (asset_->layout)
        {
            const auto evaluation = animation_.evaluate();
            if (!evaluation.succeeded())
            {
                error_ = evaluation.status().message;
                render_dirty_ = false;
                return;
            }
            if (show_bones_)
            {
                debug_lines = bone_lines(*evaluation.value());
            }
        }
        if (next_texture_ >= (1ull << 45) || next_request_ == std::numeric_limits<std::uint64_t>::max())
        {
            error_ = "Mesh preview image identifier space exhausted.";
            render_dirty_ = false;
            return;
        }
        candidate_id_ = ImGuiTextureId(next_texture_++);
        candidate_revision_ = revision_;
        candidate_preview_revision_ = preview_revision_;
        auto& request = work.animation_preview;
        request.request_id = next_request_++;
        request.texture_id = candidate_id_;
        request.extent = extent_;
        request.views.push_back(view());
        request.show_environment = preview_settings_.show_environment;
        request.render_shadows = preview_settings_.show_shadows;
        request.exposure_ev = preview_settings_.exposure_ev;
        request.debug_lines = std::move(debug_lines);
        request.debug_lines_depth_test = depth_test_;
        render_dirty_ = false;
    }

    void MeshEditorPanel::on_texture_result(const UiTextureResult& result)
    {
        if (!candidate_id_.valid() || candidate_id_ != result.texture_id)
        {
            return;
        }
        const auto completed = candidate_id_;
        candidate_id_ = {};
        if (!open_ || candidate_revision_ != revision_ || candidate_preview_revision_ != preview_revision_ ||
            !result.succeeded())
        {
            pending_work_.retire_textures.push_back(completed);
            if (!result.succeeded() && candidate_revision_ == revision_ &&
                candidate_preview_revision_ == preview_revision_)
            {
                mesh_preference_pending_ = false;
                error_ = result.error;
                scene_.clear_geometry();
                if (previous_asset_)
                {
                    auto previous = std::move(previous_asset_);
                    adopt(previous);
                    requested_id_ = asset_->id;
                }
                render_dirty_ = false;
                mesh_dirty_ = true;
            }
            return;
        }
        if (texture_id_.valid())
        {
            pending_work_.retire_textures.push_back(texture_id_);
        }
        texture_id_ = completed;
        cached_asset_ = asset_;
        cached_mesh_ = mesh_;
        if (mesh_preference_pending_ && asset_ && asset_->root_type == "toy3d.AnimationSequenceAssetData")
        {
            const auto saved = set_animation_preview_mesh_preference(workspace_.files(), asset_->id, asset_->mesh_id);
            if (!saved.succeeded())
            {
                error_ = saved.message;
            }
            else if (preview_mesh_changed_)
            {
                preview_mesh_changed_();
            }
            mesh_preference_pending_ = false;
        }
        previous_asset_.reset();
    }

    void MeshEditorPanel::seek(double time)
    {
        if (!asset_ || !asset_->sequence)
        {
            return;
        }
        animation_.set_playing(0, false);
        const auto status = animation_.seek(0, time);
        if (!status.succeeded())
        {
            error_ = status.message;
        }
        render_dirty_ = true;
    }

    void MeshEditorPanel::set_playing(bool playing)
    {
        if (!asset_ || !asset_->sequence)
        {
            return;
        }
        const auto status = animation_.set_playing(0, playing);
        if (!status.succeeded())
        {
            error_ = status.message;
        }
        render_dirty_ = true;
    }

    void MeshEditorPanel::set_preview_display(bool mesh, bool bones, bool depth_test)
    {
        mesh_dirty_ = mesh_dirty_ || show_mesh_ != mesh;
        show_mesh_ = mesh;
        show_bones_ = bones;
        depth_test_ = depth_test;
        render_dirty_ = true;
    }

    bool MeshEditorPanel::set_preview_scene_settings(const PreviewSceneSettings& settings)
    {
        if (!validate_preview_scene_settings(settings))
        {
            error_ = "Invalid mesh preview scene settings.";
            return false;
        }
        if (!(preview_settings_ == settings))
        {
            preview_settings_ = settings;
            ++preview_revision_;
            render_dirty_ = true;
            error_.clear();
        }
        return true;
    }

    const PreviewSceneSettings& MeshEditorPanel::preview_scene_settings() const
    {
        return preview_settings_;
    }

    void MeshEditorPanel::draw_bone(std::uint32_t index)
    {
        const auto& bones = asset_->layout->skeleton().bones;
        bool leaf = true;
        for (const auto& bone : bones)
        {
            leaf = leaf && bone.parent_index != static_cast<std::int32_t>(index);
        }
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (leaf)
        {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (selected_bone_ == static_cast<std::int32_t>(index))
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        ImGui::PushID(static_cast<int>(index));
        const bool expanded = ImGui::TreeNodeEx(bones[index].name.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            selected_bone_ = static_cast<std::int32_t>(index);
            render_dirty_ = true;
        }
        if (expanded)
        {
            for (std::uint32_t child = 0; child < bones.size(); ++child)
            {
                if (bones[child].parent_index == static_cast<std::int32_t>(index))
                {
                    draw_bone(child);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void MeshEditorPanel::draw_asset_details()
    {
        ImGui::TextWrapped("%s", name_for(workspace_.catalog(), asset_->id, "Asset").c_str());
        ImGui::TextDisabled("%s", asset_->static_mesh                                  ? "Static Mesh"
                                  : asset_->root_type == "toy3d.SkeletonAssetData"     ? "Skeleton"
                                  : asset_->root_type == "toy3d.SkeletalMeshAssetData" ? "Skeletal Mesh"
                                                                                       : "Animation Sequence");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", asset_->path.c_str());
        }
        if (asset_->layout && ImGui::CollapsingHeader("Skeleton", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Bones: %zu", asset_->layout->skeleton().bones.size());
        }
        if (asset_->static_mesh && ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto& geometry = *asset_->static_mesh;
            ImGui::Text("Vertices: %zu", geometry.vertices.size());
            ImGui::Text("Triangles: %zu", geometry.indices.size() / 3);
            ImGui::Text("Sections: %zu", geometry.sections.size());
            ImGui::Text("Material slots: %zu", geometry.material_slots.size());
            ImGui::Text("Tangent frame: %s", geometry.valid_tangent_frame ? "Available" : "Unavailable");
        }
        if (asset_->mesh && ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto& geometry = asset_->mesh->geometry;
            ImGui::Text("Vertices: %zu", geometry.mesh.vertices.size());
            ImGui::Text("Influences / vertex: %u", geometry.num_bone_influences);
            ImGui::Text("Sections: %zu", geometry.section_bone_maps.size());
            for (std::size_t i = 0; i < geometry.section_bone_maps.size(); ++i)
            {
                ImGui::Text("Section %zu: %zu bones", i, geometry.section_bone_maps[i].size());
            }
        }
        draw_materials();
        if (asset_->sequence && ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Duration: %.3f s", asset_->sequence->duration());
            ImGui::Text("Sample rate: %u", asset_->sample_rate);
        }
    }

    AssetStatus MeshEditorPanel::prepare_materials(const std::vector<AssetRef>& references)
    {
        if (!asset_)
        {
            return {AssetErrorCode::InvalidState, {}, {}, {}, {}, "Mesh preview is unavailable.", {}};
        }
        const auto& slots =
            asset_->static_mesh ? asset_->static_mesh->material_slots : asset_->mesh->data.material_slots;
        auto materials = load_mesh_materials(slots, references, material_, material_resolver_,
                                             asset_->static_mesh ? shader::VertexFactoryType::Local
                                                                 : shader::VertexFactoryType::GPUSkin,
                                             asset_->static_mesh ? asset_->static_mesh->valid_tangent_frame
                                                                 : asset_->mesh->geometry.mesh.valid_tangent_frame);
        if (!materials.succeeded())
        {
            return materials.status();
        }
        preview_materials_ = std::move(materials).value();
        return AssetStatus::success();
    }

    AssetStatus MeshEditorPanel::assign_material(std::size_t slot, const AssetRef& material)
    {
        const auto status = material_session_.set_material(slot, material);
        if (status.succeeded())
        {
            cached_mesh_.reset();
            mesh_dirty_ = render_dirty_ = true;
            ++preview_revision_;
        }
        return status;
    }

    AssetStatus MeshEditorPanel::undo_material()
    {
        const auto status = material_session_.undo();
        if (status.succeeded())
        {
            cached_mesh_.reset();
            mesh_dirty_ = render_dirty_ = true;
            ++preview_revision_;
        }
        return status;
    }

    AssetStatus MeshEditorPanel::redo_material()
    {
        const auto status = material_session_.redo();
        if (status.succeeded())
        {
            cached_mesh_.reset();
            mesh_dirty_ = render_dirty_ = true;
            ++preview_revision_;
        }
        return status;
    }

    AssetStatus MeshEditorPanel::save_materials()
    {
        const auto prepared = prepare_materials(material_session_.materials());
        if (!prepared.succeeded())
        {
            return prepared;
        }
        const auto status = material_session_.save();
        if (!material_session_.dirty())
        {
            if (preview_mesh_changed_)
            {
                preview_mesh_changed_();
            }
            invalidate();
        }
        return status;
    }

    void MeshEditorPanel::draw_materials()
    {
        if ((!asset_->static_mesh && !asset_->mesh) || !resource_picker_ ||
            !ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }
        const auto& slots =
            asset_->static_mesh ? asset_->static_mesh->material_slots : asset_->mesh->data.material_slots;
        const auto& values = material_session_.active() ? material_session_.materials()
                             : asset_->static_mesh      ? asset_->static_mesh->default_materials
                                                        : asset_->mesh->data.default_materials;
        ImGui::BeginDisabled(!material_session_.writable() || cpu_task_ || needs_load_);
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const AssetRef current = values.empty() ? AssetRef{} : values[i];
            AssetRef selected;
            if (draw_mesh_material_slot(*resource_picker_, workspace_, i, slots[i], current, selected, error_))
            {
                const auto status = assign_material(i, selected);
                error_ = status.succeeded() ? std::string{} : status.message;
            }
        }
        ImGui::EndDisabled();
        if (!material_session_.active() && asset_->mesh && ImGui::Button("Edit Mesh Materials"))
        {
            request_open(asset_->mesh_id);
        }
        else if (!material_session_.writable())
        {
            ImGui::TextDisabled("Mesh default materials are read-only here.");
        }
    }

    bool MeshEditorPanel::request_exit()
    {
        if (!material_session_.dirty())
        {
            return true;
        }
        pending_exit_ = pending_close_ = true;
        open_ = true;
        return false;
    }

    bool MeshEditorPanel::take_exit()
    {
        const bool ready = exit_ready_;
        exit_ready_ = false;
        return ready;
    }

    void MeshEditorPanel::draw_unsaved_prompt()
    {
        if (pending_close_ || pending_open_id_.valid())
        {
            ImGui::OpenPopup("Unsaved Mesh Materials");
        }
        if (!ImGui::BeginPopupModal("Unsaved Mesh Materials", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextUnformatted("Save material changes before closing or opening another asset?");
        bool proceed = false;
        if (ImGui::Button("Save"))
        {
            const auto status = save_materials();
            error_ = status.succeeded() ? std::string{} : status.message;
            proceed = !material_session_.dirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            material_session_.clear();
            cached_mesh_.reset();
            proceed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            pending_close_ = false;
            pending_exit_ = false;
            pending_open_id_ = {};
            ImGui::CloseCurrentPopup();
        }
        if (proceed)
        {
            const auto id = pending_open_id_;
            const bool closing = pending_close_;
            exit_ready_ = pending_exit_;
            pending_exit_ = false;
            pending_close_ = false;
            pending_open_id_ = {};
            ImGui::CloseCurrentPopup();
            if (closing)
            {
                // This frame already drew the viewport. Withdraw image identities before the next UI snapshot.
                close_next_frame_ = true;
            }
            else
            {
                request_open(id);
            }
        }
        ImGui::EndPopup();
    }

    void MeshEditorPanel::draw_bone_details()
    {
        if (selected_bone_ < 0)
        {
            ImGui::TextDisabled("Select a bone in Skeleton Tree");
            return;
        }
        const auto& bone = asset_->layout->skeleton().bones[selected_bone_];
        ImGui::TextWrapped("%s", bone.name.c_str());
        ImGui::Text("Parent: %d", bone.parent_index);
        const auto show_transform = [](const char* label, const Transform& transform)
        {
            if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto show_value = [](const char* name, const char* format, const float* value)
                {
                    if (begin_property_row(name))
                    {
                        ImGui::Text(format, value[0], value[1], value[2], value[3]);
                        end_property_row();
                    }
                };
                const float translation[] = {transform.translation.x, transform.translation.y, transform.translation.z,
                                             0};
                const float rotation[] = {transform.rotation.x, transform.rotation.y, transform.rotation.z,
                                          transform.rotation.w};
                const float scale[] = {transform.scale.x, transform.scale.y, transform.scale.z, 0};
                show_value("Location", "%.2f  %.2f  %.2f", translation);
                show_value("Rotation (Q)", "%.3f  %.3f  %.3f  %.3f", rotation);
                show_value("Scale", "%.3f  %.3f  %.3f", scale);
            }
        };
        show_transform("Reference Local", bone.reference_local_transform);
        const auto evaluated = animation_.evaluate();
        if (evaluated.succeeded())
        {
            show_transform("Current Local", evaluated.value()->local_pose.local_transforms[selected_bone_]);
            if (ImGui::CollapsingHeader("Component Space", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto position =
                    transform_position(evaluated.value()->component_pose.bone_matrices[selected_bone_], Vector3());
                ImGui::Text("Location: %.2f  %.2f  %.2f", position.x, position.y, position.z);
            }
        }
    }

    void MeshEditorPanel::draw_preview_selectors()
    {
        if (asset_->static_mesh)
        {
            if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen) &&
                property_bool("Show Mesh", &show_mesh_))
            {
                mesh_dirty_ = render_dirty_ = true;
            }
            return;
        }
        if (!resource_picker_)
        {
            return;
        }
        const auto selector = [this](const char* label, const char* type, bool mesh)
        {
            const auto current = mesh ? selected_mesh_ : selected_sequence_;
            AssetResourceSelection selected;
            const auto compatible = [this](const AssetCatalogEntry& entry)
            {
                return animation_asset_matches_layout(workspace_.types(), workspace_.files(), entry, *asset_->layout);
            };
            if (resource_picker_->draw(label, workspace_, {current, {}}, {type}, selected, error_, compatible))
            {
                select(mesh ? selected.asset : selected_mesh_, mesh ? selected_sequence_ : selected.asset);
            }
        };
        if (ImGui::CollapsingHeader("Preview Assets", ImGuiTreeNodeFlags_DefaultOpen))
        {
            selector("Preview Mesh", "toy3d.SkeletalMeshAssetData", true);
            selector("Animation", "toy3d.AnimationSequenceAssetData", false);
        }
        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (property_bool("Show Mesh", &show_mesh_))
            {
                mesh_dirty_ = render_dirty_ = true;
            }
            render_dirty_ |= property_bool("Show Bones", &show_bones_);
            render_dirty_ |= property_bool("Bone Depth Test", &depth_test_);
            render_dirty_ |= property_bool("Lock Root", &lock_root_);
        }
    }

    void MeshEditorPanel::draw_animation_browser()
    {
        ImGui::TextUnformatted("Asset Browser");
        ImGui::Separator();
        ImGui::BeginDisabled(cpu_task_ || needs_load_);
        if (ImGui::Selectable("Reference Pose", !selected_sequence_.valid()))
        {
            select(selected_mesh_, {});
        }
        for (const auto& id : compatible_sequences_)
        {
            const auto label = name_for(workspace_.catalog(), id, "Missing Animation");
            ImGui::PushID(id.hex().c_str());
            if (ImGui::Selectable(label.c_str(), id == selected_sequence_))
            {
                select(selected_mesh_, id);
            }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    }

    void MeshEditorPanel::draw_playback()
    {
        ImGui::Separator();
        if (!asset_->sequence)
        {
            if (asset_->static_mesh)
            {
                ImGui::TextDisabled("%zu triangles | %zu vertices", asset_->static_mesh->indices.size() / 3,
                                    asset_->static_mesh->vertices.size());
                return;
            }
            ImGui::TextDisabled("Reference Pose");
            return;
        }
        const auto* clock = animation_.playback_state(0);
        if (!clock)
        {
            return;
        }
        const double duration = asset_->sequence->duration();
        float time = static_cast<float>(clock->time());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##Time", &time, 0, static_cast<float>(duration), "%.3f s"))
        {
            seek(time);
        }
        if (ImGui::Button("|<##Start"))
        {
            seek(0);
        }
        ImGui::SameLine();
        if (ImGui::Button("<##Previous Sample"))
        {
            seek(std::max(0.0, clock->time() - 1.0 / asset_->sample_rate));
        }
        ImGui::SameLine();
        if (ImGui::Button(clock->playing() ? "||##Pause" : ">##Play"))
        {
            set_playing(!clock->playing());
        }
        ImGui::SameLine();
        if (ImGui::Button(">##Next Sample"))
        {
            seek(std::min(duration, clock->time() + 1.0 / asset_->sample_rate));
        }
        ImGui::SameLine();
        if (ImGui::Button(">|##End"))
        {
            seek(duration);
        }
        ImGui::SameLine();
        bool changed = ImGui::Checkbox("Loop", &playback_.loop);
        float rate = static_cast<float>(playback_.rate);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
        if (ImGui::SliderFloat("Speed", &rate, 0.1f, 4.0f, "%.2fx"))
        {
            playback_.rate = rate;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.2f s | %u fps", duration, asset_->sample_rate);
        if (changed)
        {
            const auto status = animation_.set_playback_settings(0, playback_);
            if (!status.succeeded())
            {
                error_ = status.message;
            }
            render_dirty_ = true;
        }
    }

    void MeshEditorPanel::draw_viewport()
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 size(std::max(1.0f, available.x), std::max(1.0f, available.y));
        // Keep the canvas aspect ratio within the public readback budget;
        // a larger editor window samples the same bounded GPU image.
        const float fit =
            std::min(1.0f, static_cast<float>(rhi_max_texture_readback_dimension) / std::max(size.x, size.y));
        const Extent extent{std::max(1u, static_cast<std::uint32_t>(size.x * fit)),
                            std::max(1u, static_cast<std::uint32_t>(size.y * fit))};
        if (extent.width != extent_.width || extent.height != extent_.height)
        {
            extent_ = extent;
            render_dirty_ = true;
        }
        const auto position = ImGui::GetCursorScreenPos();
        // Claim orbit/pan gestures so dragging the image cannot move the asset window.
        ImGui::InvisibleButton("Preview image", size,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        if (texture_id_.valid())
        {
            ImGui::GetWindowDrawList()->AddImage(
                reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(texture_id_.value())), position,
                ImVec2(position.x + size.x, position.y + size.y));
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        {
            auto& io = ImGui::GetIO();
            // C++17 clamp keeps pitch away from the poles and bounds camera zoom.
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                yaw_ = std::remainder(yaw_ + io.MouseDelta.x * 0.4f, 360.0f);
                pitch_ = std::clamp(pitch_ - io.MouseDelta.y * 0.4f, -80.0f, 80.0f);
                render_dirty_ = true;
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                const auto camera = view();
                const auto rotation = camera.camera_orientation();
                const Matrix4 axes = to_matrix4(rotation);
                center_ = center_ +
                          transform_vector(axes, Vector3(-io.MouseDelta.x, io.MouseDelta.y, 0)) * (distance_ * 0.0015f);
                render_dirty_ = true;
            }
            if (io.MouseWheel != 0)
            {
                distance_ = std::clamp(distance_ * std::exp(-io.MouseWheel * 0.12f), radius_ * 0.05f, radius_ * 100.0f);
                render_dirty_ = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F))
            {
                frame_all();
            }
        }
    }

    void MeshEditorPanel::draw()
    {
        if (close_next_frame_)
        {
            close_next_frame_ = false;
            close();
        }
        visible_ = false;
        if (!open_)
        {
            return;
        }
        if (focus_requested_)
        {
            ImGui::SetNextWindowFocus();
            focus_requested_ = false;
        }
        const float font = ImGui::GetFontSize();
        ImGui::SetNextWindowSize(ImVec2(font * 70.0f, font * 40.0f), ImGuiCond_FirstUseEver);
        bool keep_open = open_;
        const auto* requested = workspace_.catalog().index.find(requested_id_);
        const bool static_window =
            requested ? requested->index.root_type == "toy3d.StaticMeshAssetData" : asset_ && asset_->static_mesh;
        visible_ = ImGui::Begin(static_window ? "Static Mesh Editor###MeshEditor" : "Animation Editor###MeshEditor",
                                &keep_open);
        // Closing must not emit commands for the image identities close() withdraws.
        if (visible_ && keep_open)
        {
            if (material_session_.active())
            {
                ImGui::BeginDisabled(!material_session_.writable() || cpu_task_ || needs_load_);
                if (ImGui::Button("Save"))
                {
                    const auto status = save_materials();
                    error_ = status.succeeded() ? std::string{} : status.message;
                }
                ImGui::SameLine();
                if (ImGui::Button("Undo"))
                {
                    const auto status = undo_material();
                    error_ = status.succeeded() ? std::string{} : status.message;
                }
                ImGui::SameLine();
                if (ImGui::Button("Redo"))
                {
                    const auto status = redo_material();
                    error_ = status.succeeded() ? std::string{} : status.message;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (material_session_.dirty())
                {
                    ImGui::TextUnformatted("*");
                    ImGui::SameLine();
                }
            }
            if (ImGui::Button("Frame All (F)"))
            {
                frame_all();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset View"))
            {
                yaw_ = 25;
                pitch_ = 12;
                frame_all();
            }
            ImGui::SameLine();
            if (ImGui::Button("Show"))
            {
                ImGui::OpenPopup("Viewport Display");
            }
            if (ImGui::BeginPopup("Viewport Display"))
            {
                if (ImGui::MenuItem("Mesh", nullptr, &show_mesh_))
                {
                    mesh_dirty_ = render_dirty_ = true;
                }
                if (asset_ && asset_->layout)
                {
                    render_dirty_ |= ImGui::MenuItem("Bones", nullptr, &show_bones_);
                    render_dirty_ |= ImGui::MenuItem("Bone Depth Test", nullptr, &depth_test_);
                }
                ImGui::EndPopup();
            }
            if (asset_)
            {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", name_for(workspace_.catalog(), asset_->id, "Asset").c_str());
            }
            if (cpu_task_ || needs_load_)
            {
                ImGui::Text("Loading: %s (%.1f s)", name_for(workspace_.catalog(), requested_id_, "Asset").c_str(),
                            loading_seconds_);
            }
            if (!error_.empty())
            {
                ImGui::TextWrapped("Preview: %s", error_.c_str());
            }
            ImGui::Separator();
            if (asset_ &&
                ImGui::BeginTable("AnimationWorkspace", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                                  ImGui::GetContentRegionAvail()))
            {
                ImGui::TableSetupColumn("Skeleton", ImGuiTableColumnFlags_WidthStretch, 0.20f);
                ImGui::TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch, 0.50f);
                ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                ImGui::TableNextColumn();
                ImGui::BeginChild("SkeletonSidebar", ImVec2(0, 0));
                if (ImGui::BeginTabBar("SkeletonPanels"))
                {
                    if (asset_->layout && ImGui::BeginTabItem("Skeleton Tree"))
                    {
                        ImGui::BeginChild("Bone tree", ImVec2(0, 0));
                        ImGui::TextDisabled("%zu bones", asset_->layout->skeleton().bones.size());
                        for (std::uint32_t i = 0; i < asset_->layout->skeleton().bones.size(); ++i)
                        {
                            if (asset_->layout->skeleton().bones[i].parent_index < 0)
                            {
                                draw_bone(i);
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Asset Details"))
                    {
                        ImGui::BeginChild("AssetDetailsScroll", ImVec2(0, 0));
                        draw_asset_details();
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndChild();
                ImGui::TableNextColumn();
                ImGui::BeginChild("PreviewCenter", ImVec2(0, 0), false,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::TextUnformatted("Viewport");
                const float transport_height =
                    asset_->sequence ? ImGui::GetFrameHeightWithSpacing() * 3.0f + ImGui::GetStyle().ItemSpacing.y
                                     : ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
                ImGui::BeginChild("Animation viewport",
                                  ImVec2(0, std::max(font * 2, ImGui::GetContentRegionAvail().y - transport_height)),
                                  true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                draw_viewport();
                ImGui::EndChild();
                draw_playback();
                ImGui::EndChild();
                ImGui::TableNextColumn();
                ImGui::BeginChild("PreviewSidebar", ImVec2(0, 0));
                const float browser_height =
                    asset_->layout ? std::min(font * 9.0f, ImGui::GetContentRegionAvail().y * 0.30f) : 0;
                ImGui::BeginChild("PreviewProperties",
                                  ImVec2(0, std::max(font * 2, ImGui::GetContentRegionAvail().y - browser_height -
                                                                   ImGui::GetStyle().ItemSpacing.y)));
                if (ImGui::BeginTabBar("AnimationProperties"))
                {
                    if (ImGui::BeginTabItem("Details"))
                    {
                        ImGui::BeginChild("AnimationDetailsScroll", ImVec2(0, 0));
                        draw_preview_selectors();
                        if (asset_->layout && ImGui::CollapsingHeader("Selected Bone", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            draw_bone_details();
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Preview Scene Settings"))
                    {
                        ImGui::BeginChild("PreviewSceneScroll", ImVec2(0, 0));
                        auto settings = preview_settings_;
                        if (draw_preview_scene_settings(workspace_, settings))
                        {
                            set_preview_scene_settings(settings);
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndChild();
                if (asset_->layout)
                {
                    ImGui::BeginChild("AnimationAssetBrowser", ImVec2(0, 0), true);
                    draw_animation_browser();
                    ImGui::EndChild();
                }
                ImGui::EndChild();
                ImGui::EndTable();
            }
        }
        draw_unsaved_prompt();
        ImGui::End();
        if (!keep_open)
        {
            if (material_session_.dirty())
            {
                pending_close_ = true;
            }
            else
            {
                // This frame already references the viewport image in its UI snapshot.
                close_next_frame_ = true;
            }
        }
    }

    std::vector<ImGuiTextureId> MeshEditorPanel::texture_ids() const
    {
        return texture_id_.valid() ? std::vector<ImGuiTextureId>{texture_id_} : std::vector<ImGuiTextureId>{};
    }

    const MeshPreviewAsset* MeshEditorPanel::asset() const
    {
        return asset_.get();
    }

    const std::string& MeshEditorPanel::error() const
    {
        return error_;
    }

    void MeshEditorPanel::close()
    {
        if (material_session_.dirty())
        {
            pending_close_ = true;
            return;
        }
        material_session_.clear();
        preview_materials_.clear();
        pending_open_id_ = {};
        pending_close_ = false;
        open_ = visible_ = needs_load_ = false;
        ++revision_;
        if (texture_id_.valid())
        {
            pending_work_.retire_textures.push_back(texture_id_);
        }
        if (candidate_id_.valid())
        {
            pending_work_.retire_textures.push_back(candidate_id_);
        }
        candidate_id_ = texture_id_ = {};
        pending_work_.release_animation_preview = true;
        scene_.clear_geometry();
        mesh_.reset();
        asset_.reset();
        previous_asset_.reset();
        animation_ = {};
        requested_id_ = {};
    }

    void MeshEditorPanel::shutdown()
    {
        if (cpu_task_ && TaskGraphInterface::is_running())
        {
            const auto status = tasks_->wait_until_task_completes(cpu_task_, NamedThread::GameThread);
            if (!status.succeeded())
            {
                TOY_LOG_ERROR("Mesh preview worker shutdown failed.");
            }
        }
        cpu_task_.reset();
        cpu_result_.reset();
        resource_picker_ = nullptr;
        if (initialized_)
        {
            material_session_.clear();
            close();
            scene_.shutdown();
        }
        material_.reset();
        cached_asset_.reset();
        cached_mesh_.reset();
        environment_cube_.reset();
        loaded_environment_ = {};
        initialized_ = false;
        tasks_ = nullptr;
    }
} // namespace toy3d
