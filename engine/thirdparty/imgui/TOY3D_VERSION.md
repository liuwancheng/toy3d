# Dear ImGui vendor revision

Toy3d vendors the Dear ImGui `1.89.9` Docking core snapshot carried by Tracy
commit `37aff70dfa50cf6307b3fee6074d627dc2929143`. The source snapshot is taken
from `D:/GitProject/Vulkan-Samples/third_party/tracy/imgui`, matching the
Vulkan-Samples reference tree selected for this renderer change. The primary
`third_party/imgui` checkout in that repository is a non-Docking `1.90.5 WIP`
snapshot and is intentionally not used because Editor Docking is a required
contract.

Only `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp` and
`imgui_widgets.cpp` are compiled. Toy3d intentionally does not compile or link
official platform/renderer backends such as `imgui_impl_glfw` or
`imgui_impl_vulkan`.

Compared with the previous `1.87 WIP` snapshot, this revision retains Docking,
the queued input event API, `ImDrawCmd::IdxOffset`/`VtxOffset`, 16/32-bit index
configuration and `ImDrawCallback_ResetRenderState`. The Toy3d integration does
not require an official backend before or after the upgrade.
