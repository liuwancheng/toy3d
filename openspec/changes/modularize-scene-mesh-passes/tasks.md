## 1. 合并基线与模块骨架

- [x] 1.1 核对 `add-tonemap-imgui-output-passes` 的实际实施状态和当前未提交的 View binding reuse diff，以 BasePass→Tonemap→可选 ImGui 单 graphics list 顺序为基线记录需要保留的代码；通过 `git diff -- engine/runtime/renderscene engine/runtime/tests` 确认后续施工没有覆盖用户已有改动。
- [x] 1.2 新增 `renderscene/pass/mesh_draw_command.h`、`pass/base_pass.h/.cpp` 和 `view/scene_visibility.h/.cpp` 骨架，只定义 spec 已登记的 `MeshDrawCommand`、`MeshPassDrawList`、`BasePassInputs` 与函数入口；通过重新运行 CMake configure 并检查 `Toy3dRuntime` source list/build 证明现有 `CONFIGURE_DEPENDS` 已发现新文件，除非真实需要不得添加 `mesh_draw_command.cpp`。

## 2. Scene visibility 拆分

- [x] 2.1 将 primitive frustum culling 与每 View stale-result 清理迁入 stateless `compute_scene_visibility(...)`，保持 reversed-Z、AABB 接触平面可见和无效 primitive 可诊断跳过；通过 `Toy3dRendererSceneOwnershipTests` 覆盖双 View 不同结果、接触平面、连续计算清空旧结果与无效 bounds。
- [x] 2.2 将可见 `StaticMeshSceneProxy` 的 candidate MeshBatch gather 迁入 visibility 模块，并保证不在该阶段解析 shader、pipeline、binding 或 pass eligibility；通过测试证明可见且资源完整的 proxy 产生 candidate、被剔除/不支持/缺资源 proxy 不产生残缺 batch。
- [x] 2.3 迁移测试到公开的 stateless visibility 入口，删除针对 `ForwardSceneRenderer::compute_view_visibility` 的 private-member access，并在 renderer 集成完成后删除 `compute_view_visibility()`/`collect_mesh_batches()` 私有方法；通过 `rg "compute_view_visibility|collect_mesh_batches|PrivateMemberAccess" engine/runtime/renderscene engine/runtime/tests` 确认仅允许规范/无关测试辅助残留。

## 3. View uniform 生命周期收敛

- [x] 3.1 将 View parameter serialization、canonical ABI bytes 和 uniform-buffer upload 拆成每 `ViewInfo` 一次的准备阶段，并让 `ViewInfo` 只保存本帧 View uniform resource及必要 adapter cache；通过单元测试证明 `init_views()` 仍可纯 CPU 校验，GPU prepare 失败不发布半成品资源且保留原始 `RHIStatus`。
- [x] 3.2 将 layout-specific `RHIBindingSet` 物化改为复用已上传 View buffer，同一 View/相同 compatible layout 复用 adapter、不同 layout 只新增 adapter；通过 `Toy3dRenderResourceManagerTests` 的 instrumented fake device/context 断言多个 MeshBatch 只发生一次 View serialization/upload。
- [x] 3.3 检查 `ViewInfo` 没有 attachment、pass pipeline/draw list、command context/queue/frame slot、shadow 或 backend native 状态；通过头文件审查测试和 `rg "Vk|Vulkan|BasePass|ShadowPass|RHIRenderPass|CommandContext|FrameSlot" engine/runtime/renderscene/view/view_info.*` 确认边界。

## 4. BasePass 与 mesh draw command 落地

- [x] 4.1 实现 `MeshDrawCommand`/`MeshPassDrawList` 的 frame-local value contract和 `BasePassInputs` validation，确保 draw list 不含 attachment 或准备源指针；通过编译期/单元测试检查 value 可移动性、默认初始化和 RHI strong-ref 保活语义。
- [x] 4.2 将 BasePass 的 shader/effective state、vertex input、pipeline、View adapter、Material/Object binding 与 draw command 物化迁入 `pass/base_pass.cpp`，并保持无效单 batch 可诊断跳过；通过 `Toy3dRenderResourceManagerTests` 覆盖 vertex input 不兼容、Global/Pass source 缺失、attachment/device mismatch 和合法多 draw list。
- [x] 4.3 实现 `render_base_pass(...)` 的 prepare-then-execute 边界，确保全部 create/upload 在 `begin_render_pass()` 前完成，begin 后只消费 `MeshDrawCommand` 且失败时尽力 end并返回首个错误；通过 recording fake 的有序调用断言覆盖成功、prepare failure 和 draw failure 三条路径。
- [x] 4.4 在执行循环中继续为每 draw 设置确定的 blend constants、stencil reference、viewport/scissor、pipeline、vertex/index buffers 和完整 `RHIGraphicsBindings` snapshot，并加入相邻状态复用时不得改变语义的测试；通过 Vulkan-agnostic RHI mock 证明五 logical groups 不被解释为 physical set。

## 5. Renderer 集成与旧路径删除

- [x] 5.1 将 `ForwardSceneRenderer::render_scene_passes()` 收敛为 `init_views()`、stateless visibility、View uniform prepare、SceneColor/SceneDepth transition 和 `render_base_pass(...)` 调用；通过 renderer 集成测试验证 BasePass 后的 Tonemap/可选 ImGui 仍在同一 graphics context 和单次 business submit 中按序录制。
- [x] 5.2 删除 `PreparedBasePass`、`PreparedViewBinding`、旧 `prepare_base_pass()`/`execute_base_pass()` 及被新模块替代的 helper，不建立 alias 或双轨入口；通过 `rg "PreparedBasePass|PreparedViewBinding|prepare_base_pass|execute_base_pass|MeshDrawPacket" engine/runtime` 返回无旧正式实现命中。
- [x] 5.3 复查 BasePass/visibility 公共头文件只依赖公共 renderscene/RenderCore/RHI 类型且可独立包含，不出现 `Vk*`、`ID3D11*`、`ID3D12*` 或 backend/profile 分支；通过独立 include 编译和源码搜索验证跨后端边界。

## 6. Contract 同步与验证

- [x] 6.1 更新 `document/rhi-design.md` 的 prepare/execute、BasePass 和 `MeshDrawPacket` 段落为本 change 的 `render_base_pass(...)`、`MeshDrawCommand`/`MeshPassDrawList` contract；通过对照 `document/index.md`、binding aggregation active design 与本 change specs 确认不存在相互冲突的 Active 描述。
- [x] 6.2 运行 `npx.cmd --yes @fission-ai/openspec@1.10.0 validate "modularize-scene-mesh-passes"`，并检查所有新增具名类型均已在唯一 `Type Contracts` 登记、每个代码型子 spec 均保留 `Minimal Implementation Example`，验证必须零错误。
- [x] 6.3 使用 `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON` 配置，并构建 `Toy3dRendererSceneOwnershipTests`、`Toy3dRenderResourceManagerTests` 与 `Toy3dEditor`；所有命令必须成功且不得新增编译 warning/error。
- [x] 6.4 运行 `ctest --test-dir build -C Debug -R "Toy3dRuntime.(RendererSceneOwnership|RenderResourceManager|TonemapPass)" --output-on-failure` 后再运行完整 `ctest --test-dir build -C Debug --output-on-failure`；定向与全量测试必须全部通过。
- [x] 6.5 由独立 sub-agent 使用 `verify-toy3d-build` 复核配置、受影响 targets、CTest 和改动边界，主 agent 根据其证据修复任何问题；以独立验证报告无未解决失败作为完成条件。
- [x] 6.6 在启用 Vulkan validation 的 Debug Editor/Cube 路径完成实际 draw/present 冒烟，确认可见 mesh、BasePass→Tonemap→ImGui 顺序、窗口 resize/退出均正常且无 descriptor/lifetime validation error；在 D3D11/D3D12 后端尚未实现时保留公共 compile/contract 审计结果，不以 Vulkan 特例替代。
