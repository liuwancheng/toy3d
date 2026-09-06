## 1. 建立 Pass 消费边界基线

- [x] 1.1 盘点 `ForwardSceneRenderer::render_base_pass()` 中所有 device creation、uniform upload、可变 scene/material 读取和 graphics command 调用，记录它们的现有顺序，并用 `rg` 确认调用链只覆盖 Forward Base Pass 与对应 tests。
- [x] 1.2 扩展 recording fake 的 operation 观测，覆盖空 draw 仍 begin/end clear、pass descriptor failure 不 begin、draw command failure 仍尝试 end 且外层 discard/abort，并构建运行对应 Renderer tests 确认迁移前基线。

## 2. 建立帧内准备结果

- [x] 2.1 在 `ForwardSceneRenderer` 内引入 private incomplete `PreparedBasePass`，在实现文件中定义聚合单个 draw 的帧内值，使 descriptor、pipeline、vertex/index bindings、graphics bindings 和 draw arguments 使用一个 draw vector 保持一致；构建受影响 target 并用定向测试确认命令顺序与 draw 数量不变。
- [x] 2.2 复查 prepared value 的成员与析构边界，确认其只持有 RHI 强引用和值、不保存 device/viewport/queue/frame/scene/material/proxy/mesh 指针或跨帧状态，并用结构搜索记录结果。

## 3. 提取 Base Pass prepare

- [x] 3.1 新增 `prepare_base_pass(RHIDevice&, RHIGraphicsCommandContext&, const RHIRenderPassDesc&, PreparedBasePass&)`，迁移 pass validation、per-view/batch 遍历、shader/pipeline 创建和 binding/uniform materialization；运行 Renderer/Material/RenderResource tests 确认逐 batch 跳过与错误分类不变。
- [x] 3.2 增加定向测试证明 prepare 的 creation/upload 发生在 `begin_render_pass` 之前，pass-level failure 不产生 render-pass/draw 命令，且 prepare 不创建或 finish context、不 submit/present/wait。

## 4. 提取 Base Pass execute

- [x] 4.1 新增 `execute_base_pass(RHIGraphicsCommandContext&, const PreparedBasePass&)`，只录制 begin/end render pass、dynamic state、pipeline/buffer/binding 与 indexed draw；运行 operation-order tests 确认空 draw clear、合法 draw 顺序和首个录制错误传播不变。
- [x] 4.2 删除旧 `render_base_pass(RHIDevice&, RHIGraphicsCommandContext&, ...)` 混合入口，并用函数签名检查与 `rg` 确认 execute 不引用 `RHIDevice`、`create_*()`、RenderScene、Material、Proxy、MeshBatch、viewport、queue、submit、present 或 wait。

## 5. 串联帧所有权与失败处理

- [x] 5.1 更新 `render_frame()`，在同一个已 begin-recording graphics context 上按 attachment transition、prepare、execute、finish 的顺序执行，并用 recording test 确认仍只生成一个 immutable business list 和一次 `end_frame()`。
- [x] 5.2 覆盖 prepare 与 execute 两类失败，验证两者都保留原始 `RHIErrorCode`、discard RenderResource recording 并通过 `abort_frame()` 恰好一次消费 frame，且不发布 Ready/candidate/completion。

## 6. 文档与结构复查

- [x] 6.1 更新 `document/rhi-design.md` 的显式 Renderer/业务 Pass 摘要，明确 prepare 可使用 device 与当前 context、execute 仅消费 context 和帧内准备值，并检查 `document/index.md` 的 Active 入口仍唯一有效。
- [x] 6.2 按 Vulkan、D3D12、D3D11 FL11_0 与 VulkanPortable v1 复核创建/录制/提交边界，确认公共 RHI 无变更、renderscene 无 backend 类型、无通用 scheduler/RDG/packet/cache 新入口，并运行 `git diff --check`。

## 7. 集成验证

- [x] 7.1 主 agent 完成 Windows CMake configure，构建受影响 Renderer/RHI tests 与 `Toy3dEditor`，运行全量 CTest，并记录测试数量与结果。
- [x] 7.2 主 agent 运行 Vulkan validation smoke，覆盖多帧、resize、minimize/restore 与正常关闭，确认 draw/present 行为不变且无 validation warning/error。
- [x] 7.3 由独立 sub-agent 使用 `verify-toy3d-build` 重新执行与风险相称的配置、构建、全量测试和 Vulkan validation smoke，主 agent 复核证据后再标记完成。
- [x] 7.4 运行 `openspec validate "stabilize-render-pass-rhi-consumption" --strict`，确认 proposal、delta spec、design 与 tasks 一致且完成状态与实际证据相符。
