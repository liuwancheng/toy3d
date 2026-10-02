#include "editor.h"

#include <exception>

#include "imgui.h"
#include "input/input_system.h"
#include "logging/logger.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    bool EditorApplication::can_start_play() const
    {
        return play_scene_ && !play_session_.active() && !startup_pending_ && !shaders_.busy() &&
               !model_import_.active() && !texture_import_.active() && !material_create_.active() &&
               !shader_create_.active() && !material_editor_.modal_pending() &&
               !material_editor_.edit_session().gesturing() && !scene_session_.history().active() &&
               !show_new_project_ && !show_project_settings_ && !show_scene_save_as_ && !waiting_material_project_ &&
               pending_scene_action_ == SceneAction::None && !scene_confirm_requested_ &&
               ImGui::GetDragDropPayload() == nullptr && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    }

    void EditorApplication::stop_play()
    {
        scene_viewport_.release_game_input();
        scene_viewport_.cancel_pending_hit();
        InputSystem::get_instance().set_capture_policy({true, true, true});
        InputSystem::get_instance().clear_pressed_state();
        play_session_.stop();
    }

    void EditorApplication::tick_play(double delta_seconds)
    {
        const auto action = play_session_.take_action();
        if (action == EditorPlayAction::Stop)
        {
            stop_play();
        }
        else if (action == EditorPlayAction::Pause)
        {
            play_session_.pause();
            scene_viewport_.release_game_input();
        }
        else if (action == EditorPlayAction::Resume)
        {
            play_session_.resume();
        }
        else if (action == EditorPlayAction::Play && can_start_play())
        {
            SceneAssetData data;
            if (!scene_session_.capture(data))
            {
                TOY_LOG_ERROR("Play Scene capture: {}", scene_session_.error());
                return;
            }
            scene_viewport_.cancel_pending_hit();
            InputSystem::get_instance().clear_pressed_state();
            const auto defaults = actor_factory_.default_material()->material();
            try
            {
                if (!play_session_.start(
                        data, workspace_, actor_factory_.actor_types(),
                        [this, defaults](const std::string& name)
                        {
                            return shader_workflow_ready_
                                       ? shaders_.shader_map(name)
                                       : (name == defaults->desc().shader_name ? defaults->desc().shader_map : nullptr);
                        },
                        *play_scene_))
                {
                    TOY_LOG_ERROR("Play Scene: {}", play_session_.error());
                }
            }
            catch (const std::exception& exception)
            {
                stop_play();
                TOY_LOG_ERROR("Play Scene initialization: {}", exception.what());
            }
        }
        if (play_session_.active())
        {
            play_session_.tick(delta_seconds);
            if (!play_session_.active())
            {
                stop_play();
                TOY_LOG_ERROR("Play Scene: {}", play_session_.error());
            }
        }
    }

    bool EditorApplication::game_viewport_input(bool& mouse, bool& keyboard) const
    {
        // Editor suppresses game bindings outside a focused running viewport.
        // Engine applies this after ImGui end_frame so its capture cannot overwrite it.
        const bool captured = play_session_.state() == EditorPlayState::Playing &&
                              scene_viewport_.game_input_captured() && !ImGui::GetIO().AppFocusLost &&
                              !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        mouse = captured && scene_viewport_.game_mouse_input();
        keyboard = captured;
        return true;
    }
} // namespace toy3d
