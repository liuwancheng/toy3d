# ImGuizmo vendor revision

Toy3d vendors the `1.10` tag of
[CedricGuillemet/ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo/tree/1.10).
Only `src/ImGuizmo.h`, `src/ImGuizmo.cpp`, and `LICENSE` are copied.

The `CanActivate()` condition in `ImGuizmo.cpp` is adapted for Toy3d's
`ImGui::Image` scene viewport. An image remains a hovered ImGui item while the
mouse is over a gizmo handle, so the upstream `IsAnyItemHovered()` check would
prevent handle activation. The existing ImGuizmo rect and handle hit tests still
limit activation to a gizmo control.
