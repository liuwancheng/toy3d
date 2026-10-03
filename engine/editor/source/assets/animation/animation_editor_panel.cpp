#include "assets/animation/animation_editor_panel.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <limits>
#include <utility>

#include "imgui.h"
#include "asset/texture/builtin_texture_assets.h"
#include "gamescene/component/skeletal_mesh_component.h"
#include "drivers/rhi/rhi_command_descriptors.h"
#include "logging/logger.h"
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
    struct AnimationEditorPanel::CpuResult
    {
        std::uint64_t revision = 0;
        std::shared_ptr<const AnimationPreviewAsset> asset;
        std::string error;
    };

    // --------------------------------------------------------------------------
    // AnimationEditorPanel: associated skeleton, mesh and sequence preview
    // --------------------------------------------------------------------------
    AnimationEditorPanel::AnimationEditorPanel(EditorWorkspace& workspace) : workspace_(workspace)
    {
        playback_.autoplay = false;
    }

    AnimationEditorPanel::~AnimationEditorPanel()
    {
        shutdown();
    }

    bool AnimationEditorPanel::initialize(SceneInterface& scene, MaterialInstanceRef material,
                                          TaskGraphInterface& tasks)
    {
        SceneEnvironmentSettings environment;
        if (!AssetId::parse(builtin_studio_environment_id, environment.environment.asset_id))
        {
            return false;
        }
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
        initialized_ = scene_.initialize(scene, material_, environment, cube.value()) && scene_.configure_thumbnail();
        if (!initialized_)
        {
            scene_.shutdown();
        }
        return initialized_;
    }

    void AnimationEditorPanel::request_open(const AssetId& id, bool focus)
    {
        if (!id.valid())
        {
            return;
        }
        open_ = visible_ = true;
        focus_requested_ = focus;
        if (requested_id_ == id && (asset_ || cpu_task_ || needs_load_))
        {
            return;
        }
        requested_id_ = id;
        override_selection_ = false;
        ++revision_;
        needs_load_ = true;
        error_.clear();
    }

    void AnimationEditorPanel::invalidate()
    {
        cached_asset_.reset();
        cached_mesh_.reset();
        if (open_ && requested_id_.valid())
        {
            ++revision_;
            needs_load_ = true;
        }
    }

    void AnimationEditorPanel::select(const AssetId& mesh, const AssetId& sequence)
    {
        selected_mesh_ = mesh;
        selected_sequence_ = sequence;
        override_selection_ = true;
        ++revision_;
        needs_load_ = true;
        error_.clear();
    }

    bool AnimationEditorPanel::adopt(std::shared_ptr<const AnimationPreviewAsset> candidate)
    {
        AnimationInstance animation;
        std::vector<AnimationSequenceInput> sources;
        if (candidate->sequence)
        {
            sources.push_back({candidate->sequence, playback_});
        }
        const auto status = animation.set_sources(candidate->layout, sources);
        if (!status.succeeded())
        {
            error_ = status.message;
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
            const auto created = SkeletalMesh::create(
                candidate->layout, *candidate->mesh,
                std::vector<MaterialInterfaceRef>(candidate->mesh->data.material_slots.size(), material_));
            if (!created.succeeded())
            {
                error_ = created.status().message;
                return false;
            }
            mesh = created.value();
        }
        asset_ = std::move(candidate);
        mesh_ = std::move(mesh);
        animation_ = std::move(animation);
        selected_mesh_ = asset_->mesh_id;
        selected_sequence_ = asset_->sequence_id;
        selected_bone_ = -1;
        mesh_dirty_ = render_dirty_ = true;
        tab_ = asset_->root_type == "toy3d.SkeletonAssetData"       ? 0
               : asset_->root_type == "toy3d.SkeletalMeshAssetData" ? 1
                                                                    : 2;
        frame_all();
        return true;
    }

    bool AnimationEditorPanel::prepare_mesh()
    {
        if (show_mesh_ && mesh_)
        {
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

    void AnimationEditorPanel::tick(double delta_seconds)
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
                result->error = "Animation preview worker failed.";
            }
            cpu_task_.reset();
            if (open_ && result->revision == revision_)
            {
                if (result->error.empty() &&
                    !animation_preview_asset_current(workspace_.asset_pairs(), workspace_.catalog(), *result->asset))
                {
                    result->error = "Preview inputs changed during load; rescan and reopen.";
                }
                if (!result->error.empty())
                {
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
            const auto catalog = workspace_.catalog();
            const auto id = requested_id_;
            const auto mesh = selected_mesh_;
            const auto sequence = selected_sequence_;
            const bool override_selection = override_selection_;
            const auto reusable = cached_asset_;
            try
            {
                cpu_task_ = dispatch_graph_task(
                    *tasks_, "Load animation preview",
                    [result, pairs, catalog, id, mesh, sequence, override_selection, reusable](NamedThread,
                                                                                               const GraphEventRef&)
                    {
                        const auto started = std::chrono::steady_clock::now();
                        try
                        {
                            auto loaded = load_animation_preview_asset(*pairs, catalog, id, override_selection, mesh,
                                                                       sequence, reusable);
                            if (loaded.succeeded())
                            {
                                result->asset =
                                    std::make_shared<const AnimationPreviewAsset>(std::move(loaded).value());
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
                        TOY_LOG_INFO("Animation preview load [{}] {} in {} ms{}{}",
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
        if (open_ && visible_ && asset_ && !needs_load_ && !cpu_task_)
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

    void AnimationEditorPanel::frame_all()
    {
        if (!asset_)
        {
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

    SceneView AnimationEditorPanel::view() const
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

    std::vector<DebugLineVertex> AnimationEditorPanel::bone_lines(const AnimationEvaluation& evaluation) const
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

    void AnimationEditorPanel::collect_render_work(UiRenderWork& work)
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
        if (mesh_dirty_ && !prepare_mesh())
        {
            render_dirty_ = false;
            return;
        }
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
        const auto evaluation = animation_.evaluate();
        if (!evaluation.succeeded())
        {
            error_ = evaluation.status().message;
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
        if (next_texture_ >= (1ull << 45) || next_request_ == std::numeric_limits<std::uint64_t>::max())
        {
            error_ = "Animation preview image identifier space exhausted.";
            render_dirty_ = false;
            return;
        }
        candidate_id_ = ImGuiTextureId(next_texture_++);
        candidate_revision_ = revision_;
        auto& request = work.animation_preview;
        request.request_id = next_request_++;
        request.texture_id = candidate_id_;
        request.extent = extent_;
        request.views.push_back(view());
        if (show_bones_)
        {
            request.debug_lines = bone_lines(*evaluation.value());
        }
        request.debug_lines_depth_test = depth_test_;
        render_dirty_ = false;
    }

    void AnimationEditorPanel::on_texture_result(const UiTextureResult& result)
    {
        if (!candidate_id_.valid() || candidate_id_ != result.texture_id)
        {
            return;
        }
        const auto completed = candidate_id_;
        candidate_id_ = {};
        if (!open_ || candidate_revision_ != revision_ || !result.succeeded())
        {
            pending_work_.retire_textures.push_back(completed);
            if (!result.succeeded() && candidate_revision_ == revision_)
            {
                error_ = result.error;
                scene_.clear_mesh();
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
        previous_asset_.reset();
    }

    void AnimationEditorPanel::seek(double time)
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

    void AnimationEditorPanel::set_playing(bool playing)
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

    void AnimationEditorPanel::set_preview_display(bool mesh, bool bones, bool depth_test)
    {
        mesh_dirty_ = mesh_dirty_ || show_mesh_ != mesh;
        show_mesh_ = mesh;
        show_bones_ = bones;
        depth_test_ = depth_test;
        render_dirty_ = true;
    }

    void AnimationEditorPanel::draw_bone(std::uint32_t index)
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

    void AnimationEditorPanel::draw()
    {
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
        ImGui::SetNextWindowSize(ImVec2(1000, 750), ImGuiCond_FirstUseEver);
        bool keep_open = open_;
        visible_ = ImGui::Begin("Animation Editor", &keep_open);
        // Begin can report content after its close button clears keep_open.
        // Do not emit image commands whose registered identity close() will withdraw.
        if (visible_ && keep_open)
        {
            if (cpu_task_ || needs_load_)
            {
                ImGui::Text("Loading mesh and animation assets... %.1f s", loading_seconds_);
                ImGui::TextWrapped("%s", name_for(workspace_.catalog(), requested_id_, "Animation asset").c_str());
            }
            if (!error_.empty())
            {
                ImGui::TextWrapped("Preview: %s", error_.c_str());
            }
            if (asset_)
            {
                ImGui::TextUnformatted(asset_->path.c_str());
                const char* tabs[] = {"Skeleton", "Skeletal Mesh", "Animation"};
                for (int i = 0; i < 3; ++i)
                {
                    if (i)
                    {
                        ImGui::SameLine();
                    }
                    if (ImGui::Selectable(tabs[i], tab_ == i, 0, ImVec2(130, 0)))
                    {
                        tab_ = i;
                    }
                }
                const auto& catalog = workspace_.catalog();
                auto selector = [this, &catalog](const char* label, const char* type, bool mesh)
                {
                    const auto selected = mesh ? selected_mesh_ : selected_sequence_;
                    const auto preview = name_for(catalog, selected, mesh ? "Skeleton only" : "Reference pose");
                    if (ImGui::BeginCombo(label, preview.c_str()))
                    {
                        if (ImGui::Selectable(mesh ? "Skeleton only" : "Reference pose", !selected.valid()))
                        {
                            select(mesh ? AssetId() : selected_mesh_, mesh ? selected_sequence_ : AssetId());
                        }
                        for (const auto& entry : catalog.entries)
                        {
                            if (entry.file.root_type == type &&
                                animation_asset_uses_skeleton(entry.file, asset_->layout->skeleton_id()))
                            {
                                ImGui::PushID(entry.file.asset_id.hex().c_str());
                                if (ImGui::Selectable(entry.path.utf8().c_str(), selected == entry.file.asset_id))
                                {
                                    select(mesh ? entry.file.asset_id : selected_mesh_,
                                           mesh ? selected_sequence_ : entry.file.asset_id);
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndCombo();
                    }
                };
                selector("Preview mesh", "toy3d.SkeletalMeshAssetData", true);
                selector("Sequence", "toy3d.AnimationSequenceAssetData", false);
                if (ImGui::Checkbox("Mesh", &show_mesh_))
                {
                    mesh_dirty_ = render_dirty_ = true;
                }
                ImGui::SameLine();
                render_dirty_ |= ImGui::Checkbox("Bones", &show_bones_);
                ImGui::SameLine();
                render_dirty_ |= ImGui::Checkbox("Depth test", &depth_test_);
                ImGui::SameLine();
                if (ImGui::Button("Frame All (F)"))
                {
                    frame_all();
                }
                if (asset_->sequence)
                {
                    const auto* clock = animation_.playback_state(0);
                    if (ImGui::Button(clock && clock->playing() ? "Pause" : "Play"))
                    {
                        set_playing(!clock->playing());
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("< Sample"))
                    {
                        seek(std::max(0.0, clock->time() - 1.0 / asset_->sample_rate));
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Sample >"))
                    {
                        seek(std::min(asset_->sequence->duration(), clock->time() + 1.0 / asset_->sample_rate));
                    }
                    ImGui::SameLine();
                    bool playback_changed = ImGui::Checkbox("Loop", &playback_.loop);
                    ImGui::SameLine();
                    float rate = static_cast<float>(playback_.rate);
                    ImGui::SetNextItemWidth(120);
                    if (ImGui::SliderFloat("Rate", &rate, 0.1f, 4.0f))
                    {
                        playback_.rate = rate;
                        playback_changed = true;
                    }
                    if (playback_changed)
                    {
                        animation_.set_playback_settings(0, playback_);
                    }
                    ImGui::SameLine();
                    render_dirty_ |= ImGui::Checkbox("Lock root", &lock_root_);
                    float time = static_cast<float>(clock->time());
                    if (ImGui::SliderFloat("Time (s)", &time, 0, static_cast<float>(asset_->sequence->duration()),
                                           "%.3f"))
                    {
                        seek(time);
                    }
                    ImGui::Text("%.3f s | %u samples/s", asset_->sequence->duration(), asset_->sample_rate);
                }
                ImGui::BeginChild("Bone tree", ImVec2(230, 0), true);
                if (tab_ == 0)
                {
                    ImGui::Text("%zu bones", asset_->layout->skeleton().bones.size());
                    for (std::uint32_t i = 0; i < asset_->layout->skeleton().bones.size(); ++i)
                    {
                        if (asset_->layout->skeleton().bones[i].parent_index < 0)
                        {
                            draw_bone(i);
                        }
                    }
                    if (selected_bone_ >= 0)
                    {
                        const auto& bone = asset_->layout->skeleton().bones[selected_bone_];
                        const auto evaluated = animation_.evaluate();
                        ImGui::Separator();
                        ImGui::TextWrapped("%s (parent %d)", bone.name.c_str(), bone.parent_index);
                        const auto show_transform = [](const char* label, const Transform& transform)
                        {
                            ImGui::TextUnformatted(label);
                            ImGui::Text("T: %.2f %.2f %.2f", transform.translation.x, transform.translation.y,
                                        transform.translation.z);
                            ImGui::Text("Q: %.3f %.3f %.3f %.3f", transform.rotation.x, transform.rotation.y,
                                        transform.rotation.z, transform.rotation.w);
                            ImGui::Text("S: %.3f %.3f %.3f", transform.scale.x, transform.scale.y, transform.scale.z);
                        };
                        show_transform("Reference local", bone.reference_local_transform);
                        if (evaluated.succeeded())
                        {
                            const auto& local = evaluated.value()->local_pose.local_transforms[selected_bone_];
                            const auto position = transform_position(
                                evaluated.value()->component_pose.bone_matrices[selected_bone_], Vector3());
                            show_transform("Current local", local);
                            ImGui::Text("Component T: %.2f %.2f %.2f", position.x, position.y, position.z);
                        }
                    }
                }
                else if (tab_ == 1 && asset_->mesh)
                {
                    const auto& data = asset_->mesh->geometry;
                    ImGui::Text("%zu vertices", data.mesh.vertices.size());
                    ImGui::Text("%u influences/vertex", data.num_bone_influences);
                    for (std::size_t i = 0; i < data.section_bone_maps.size(); ++i)
                    {
                        ImGui::Text("Section %zu: %zu bones", i, data.section_bone_maps[i].size());
                    }
                    for (const auto& slot : asset_->mesh->data.material_slots)
                    {
                        ImGui::TextWrapped("Material: %s", slot.c_str());
                    }
                }
                else
                {
                    ImGui::TextWrapped("Preview only. Assets and the level are unchanged.");
                }
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("Animation viewport", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
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
                if (texture_id_.valid())
                {
                    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(texture_id_.value())), size);
                }
                else
                {
                    ImGui::InvisibleButton("Preview image", size);
                }
                if (ImGui::IsItemHovered())
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
                        center_ = center_ + transform_vector(axes, Vector3(-io.MouseDelta.x, io.MouseDelta.y, 0)) *
                                                (distance_ * 0.0015f);
                        render_dirty_ = true;
                    }
                    if (io.MouseWheel != 0)
                    {
                        distance_ =
                            std::clamp(distance_ * std::exp(-io.MouseWheel * 0.12f), radius_ * 0.05f, radius_ * 100.0f);
                        render_dirty_ = true;
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_F))
                    {
                        frame_all();
                    }
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
        if (!keep_open)
        {
            close();
        }
    }

    std::vector<ImGuiTextureId> AnimationEditorPanel::texture_ids() const
    {
        return texture_id_.valid() ? std::vector<ImGuiTextureId>{texture_id_} : std::vector<ImGuiTextureId>{};
    }

    const AnimationPreviewAsset* AnimationEditorPanel::asset() const
    {
        return asset_.get();
    }

    const std::string& AnimationEditorPanel::error() const
    {
        return error_;
    }

    void AnimationEditorPanel::close()
    {
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
        scene_.clear_mesh();
        mesh_.reset();
        asset_.reset();
        previous_asset_.reset();
        animation_ = {};
        requested_id_ = {};
    }

    void AnimationEditorPanel::shutdown()
    {
        if (cpu_task_ && TaskGraphInterface::is_running())
        {
            const auto status = tasks_->wait_until_task_completes(cpu_task_, NamedThread::GameThread);
            if (!status.succeeded())
            {
                TOY_LOG_ERROR("Animation preview worker shutdown failed.");
            }
        }
        cpu_task_.reset();
        cpu_result_.reset();
        if (initialized_)
        {
            close();
            scene_.shutdown();
        }
        material_.reset();
        cached_asset_.reset();
        cached_mesh_.reset();
        initialized_ = false;
        tasks_ = nullptr;
    }
} // namespace toy3d
