#include "viewport/editor_viewport_gizmo.h"

#include "imgui.h"
#include "ImGuizmo.h"

#include "gamescene/component/scene_component.h"
#include "logging/logger.h"
#include "math/transform.h"

namespace toy3d
{
    namespace
    {
        // ImGuizmo defaults to 0.10; a larger screen-space size keeps the
        // translation axes usable in the Editor's wide scene viewport.
        constexpr float k_editor_gizmo_size_clip_space = 0.15f;
    } // namespace

    void EditorViewportGizmo::begin_frame()
    {
        ImGuizmo::BeginFrame();
    }

    void EditorViewportGizmo::draw_toolbar()
    {
        if (ImGui::RadioButton("Move", operation_ == Operation::Translate))
        {
            operation_ = Operation::Translate;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", operation_ == Operation::Rotate))
        {
            operation_ = Operation::Rotate;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", operation_ == Operation::Scale))
        {
            operation_ = Operation::Scale;
        }
        ImGui::SameLine();
        if (operation_ == Operation::Scale)
        {
            ImGui::TextDisabled("Local scale");
        }
        else
        {
            ImGui::Checkbox("Local", &local_mode_);
        }
    }

    void EditorViewportGizmo::handle_shortcuts(bool viewport_hovered)
    {
        if (!viewport_hovered || ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput)
        {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W))
        {
            operation_ = Operation::Translate;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_E))
        {
            operation_ = Operation::Rotate;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_R))
        {
            operation_ = Operation::Scale;
        }
    }

    bool EditorViewportGizmo::manipulate(SceneComponent& root, const Matrix4& view, const Matrix4& projection, float x,
                                         float y, float width, float height)
    {
        if (width <= 0.0f || height <= 0.0f)
        {
            return false;
        }

        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
        switch (operation_)
        {
        case Operation::Translate:
            operation = ImGuizmo::TRANSLATE;
            break;
        case Operation::Rotate:
            operation = ImGuizmo::ROTATE;
            break;
        case Operation::Scale:
            operation = ImGuizmo::SCALE;
            break;
        }

        ImGuizmo::SetDrawlist();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(x, y, width, height);
        ImGuizmo::SetGizmoSizeClipSpace(k_editor_gizmo_size_clip_space);

        // Toy3d stores column-major matrices for column vectors. The same 16
        // floats are the transposed row-vector form consumed by ImGuizmo.
        Matrix4 edited_world = root.world_transform();
        const ImGuizmo::MODE mode = local_mode_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        const bool changed = ImGuizmo::Manipulate(view.data(), projection.data(), operation, mode, edited_world.data());
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool inside_viewport = mouse.x >= x && mouse.y >= y && mouse.x < x + width && mouse.y < y + height;
        const bool consumes_pointer = ImGuizmo::IsUsingAny() || (inside_viewport && ImGuizmo::IsOver());
        if (!changed)
        {
            return consumes_pointer;
        }

        Matrix4 local_matrix = edited_world;
        if (root.parent() != nullptr)
        {
            Matrix4 inverse_parent;
            if (!try_inverse(root.parent()->world_transform(), inverse_parent))
            {
                TOY_LOG_ERROR("Gizmo cannot invert the selected component's parent transform.");
                return consumes_pointer;
            }
            local_matrix = inverse_parent * edited_world;
        }

        Transform local_transform;
        if (!try_decompose_transform(local_matrix, local_transform))
        {
            TOY_LOG_ERROR("Gizmo produced a transform that cannot be represented as local TRS.");
            return consumes_pointer;
        }
        if (!root.set_local_transform(local_transform))
        {
            TOY_LOG_ERROR("Gizmo produced an invalid local Transform.");
        }
        return consumes_pointer;
    }
} // namespace toy3d
