#include "editor.h"

#include "imgui.h"
#include "asset/asset_descriptor_path.h"
#include "logging/logger.h"
#include "platform/platform_defines.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    void EditorApplication::draw_main_menu()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Tools"))
            {
                if (ImGui::BeginMenu("Scene"))
                {
                    if (ImGui::MenuItem("New Scene")) request_scene_action(SceneAction::New);
                    if (ImGui::BeginMenu("Open Scene"))
                    {
                        for (const AssetCatalogEntry& entry : workspace_.catalog().entries)
                            if (asset_descriptor_kind(entry.path) == AssetDescriptorKind::Scene &&
                                ImGui::MenuItem(entry.path.utf8().c_str()))
                                request_scene_action(SceneAction::Open, entry.file.asset_id);
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                    {
                        if (scene_session_.path().empty()) show_scene_save_as_ = true;
                        else save_scene(scene_session_.path(), false);
                    }
                    if (ImGui::MenuItem("Save Scene As...")) show_scene_save_as_ = true;
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Create", !model_import_.active() && !texture_import_.active() && !material_create_.active() && !shader_create_.active()))
                {
                    if (ImGui::MenuItem("Create Material...")) material_create_.request(MaterialAssetCreationKind::Material, asset_folder_);
                    if (ImGui::MenuItem("Create Material Instance...")) material_create_.request(MaterialAssetCreationKind::MaterialInstance, asset_folder_);
                    if (ImGui::MenuItem("Shader...", nullptr, false, shader_workflow_ready_ && !shaders_.busy())) shader_create_.request();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Import", !shader_create_.active()))
                {
#if WITH_MODEL_IMPORT
                    if (ImGui::MenuItem("Import Static Mesh...", nullptr, false,
                        !material_create_.active() && !shader_create_.active() && !texture_import_.active()))
                    {
                        if (!model_import_.request(asset_folder_))
                        {
                            model_error_ = model_import_.error();
                            TOY_LOG_ERROR("Request model import: {}", model_error_);
                        }
                    }
#endif
                    if (ImGui::MenuItem("Import Texture2D...", nullptr, false,
                        !model_import_.active() && !material_create_.active() && !shader_create_.active()))
                    {
                        if (!texture_import_.request(asset_folder_))
                        {
                            model_error_ = texture_import_.error();
                            TOY_LOG_ERROR("Request Texture2D import: {}", model_error_);
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Shaders"))
                {
                    if (ImGui::MenuItem("Recompile Shaders", nullptr, false, shader_workflow_ready_ && !shaders_.busy())) shaders_.recompile_all();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Compile all registered saved sources (Vulkan ES3.1/default permutation).");
                    if (ImGui::MenuItem("Cancel Compilation", nullptr, false,
                        shaders_.busy() && !shaders_.task_status().restoring && shaders_.task_status().phase != ShaderTaskPhase::Cancelling)) shaders_.cancel();
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Open Log Directory")) console_.open_log_directory();
                if (ImGui::MenuItem("Exit")) request_scene_action(SceneAction::Exit);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) undo_edit();
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) redo_edit();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("Reset Layout")) reset_dock_layout_ = true;
                ImGui::Separator();
                panels_.draw_window_menu();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About Toy3d Editor")) ImGui::OpenPopup("About Toy3d Editor");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }
}
