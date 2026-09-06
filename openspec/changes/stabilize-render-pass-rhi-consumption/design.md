## Context

见 [proposal.md](./proposal.md) 的动机与 [delta spec](./specs/game-render-framework/view-render-flow/spec.md) 的行为 contract。当前 `ForwardSceneRenderer::render_base_pass()` 已在进入 native render pass 前把 pipeline、binding 和 draw 参数临时收集到多组平行 vector，但准备循环与执行循环仍位于同一函数，并且函数签名同时暴露 `RHIDevice&` 与 `RHIGraphicsCommandContext&`。

准备并非纯 CPU 阶段：View、Object 和部分 Material constant materialization 会通过 `RHIDevice` 创建 upload destination/binding，并向当前 context 录制 frame-local upload。因此本设计不能简单把所有 RHI 行为推迟到 execute，也不能让 execute 通过 context 反向取得 device。正确边界是“准备可创建对象并预录制数据传输，执行只录制 pass/draw 命令”，两者使用同一个 recording context。

UE4.27 的借鉴仅是 `FDynamicRHI` 资源创建与 `IRHICommandContext` 命令执行分离；不引入 UE 的 mesh draw command cache、RHI thread 或 command replay。Vulkan、D3D12、D3D11 FL11_0 与 VulkanPortable v1 都可以在同一 logical Render Thread 上按该顺序串行执行。

## Goals / Non-Goals

**Goals:**

- 让 Base Pass execute 的唯一 RHI collaborator 是调用方提供的 `RHIGraphicsCommandContext&`。
- 用一个具名、帧内的准备结果替代多组靠下标保持一致的平行 vector，并明确它持有命令需要的 RHI 强引用和值状态。
- 让 prepare failure、execute failure 与外层 frame abort/discard 边界可分别测试。
- 保持当前单 context、单 list、逐 batch 跳过和 render-pass clear 行为。

**Non-Goals:**

- 不修改公共 `RHIDevice`、`RHIGraphicsCommandContext`、`RHICommandList`、viewport 或 queue API。
- 不把所有 RenderResource upload 或 Material candidate publication 改造成新的 prepare/execute framework。
- 不实现通用 Pass 基类、Pass Scheduler、RDG、并行录制、跨帧 draw-command cache 或 persistent PSO cache。
- 不改变 Global/Pass binding source、ShaderMap 格式、Material 参数模型或 attachment 组合。

## Decisions

### 1. 准备结果是 ForwardSceneRenderer 私有的帧内值，不是公共 Pass API

在 `ForwardSceneRenderer` 内声明不完整的私有 `PreparedBasePass`，在实现文件中定义它；每个 draw 使用一个 `PreparedBasePassDraw` 聚合 viewport、scissor、pipeline、vertex/index bindings、graphics bindings 与 indexed draw arguments，pass 自身保存 `RHIRenderPassDesc` 和 draw vector。

这两个名词表达真实的阶段产物、强引用和帧内生命周期，不是为绕过模板或访问控制创建的 Token/Storage。它们不进入公共 RHI 或 renderscene 公共接口，也不被 Renderer 长期持有。相比继续使用多组平行 vector，单 draw 值对象消除长度和下标错配；相比新增 `ForwardBasePass` 公共类或通用 packet hierarchy，私有值保持本 change 的业务范围。

### 2. 使用显式输出参数拆 prepare，避免 incomplete type 和机制性 factory

私有方法采用近似以下形状：

```cpp
RHIStatus prepare_base_pass(
    RHIDevice& device,
    RHIGraphicsCommandContext& context,
    const RHIRenderPassDesc& pass_desc,
    PreparedBasePass& prepared_pass);

RHIStatus execute_base_pass(
    RHIGraphicsCommandContext& context,
    const PreparedBasePass& prepared_pass);
```

`PreparedBasePass` 在头文件只做 nested forward declaration，引用参数允许其定义留在 `.cpp`。不用 `std::optional`、heap-allocated opaque object、private-constructor enabler 或额外 factory。prepare 只有全部 pass-level validation 成功时才写入可执行 pass；逐 batch 不可用仍按现有 contract 记录诊断并跳过。

### 3. prepare 负责所有 device policy 和可变源解析

prepare 执行以下工作：验证 pass descriptor；遍历已经收集的 per-view `MeshBatch`；读取 Material/Shader/Proxy/View 等 RT-only source；创建或查询 shader、pipeline、binding；录制 constant upload；构造每个 `PreparedBasePassDraw`。Shader program 的本次 pass 局部去重继续留在 prepare。

准备结果不保存 `MeshBatch*`、`MaterialRenderProxy*`、`PrimitiveSceneProxy*`、`ViewInfo*` 或 `RHIDevice*`。index binding、pipeline 与 binding set 内的 resource refs 足以表达 execute 和 command-list retain 所需的 RHI 生命周期。相比让 execute 继续读取 Proxy 以减少复制，固化值可以保证 execute 不受可变准备源影响，并为未来把完整 pass 作为录制任务提供可迁移形状。

### 4. execute 只翻译准备结果为 context 命令

execute 首先用准备结果中的 descriptor 调用 `begin_render_pass()`，随后逐 draw 设置 pipeline、viewport、scissor、white blend constants、zero stencil reference、vertex/index buffers、graphics bindings 并调用 `draw_indexed()`，最后调用 `end_render_pass()`。即使 draw 列表为空也保持 begin/end，以保留 attachment clear/store 行为。

execute 不接收 device 或上层场景对象，不调用任何 `create_*()`。若 begin 失败则直接返回；begin 成功后任一 draw command 失败，仍尝试一次 `end_render_pass()`，并优先返回最先发生的 draw 错误，保持当前诊断语义。`end_render_pass()` 单独失败时返回其原始错误。

### 5. render_frame 明确串联 prepare 与 execute，并保持同一 recording

`render_frame()` 在 pending upload、View 初始化、visibility、MeshBatch 收集和 attachment transition 后构造局部准备结果，先调用 prepare，再调用 execute。二者共享刚由 frame 创建并已 begin recording 的同一个 graphics context；prepare 录制的 constant upload 因而严格位于 execute draw 之前。

任一阶段失败都走现有 `abort_recording`：RenderResourceManager discard 当前 collection，viewport `abort_frame()` 消费 frame ownership。成功后仍只 finish 一个 immutable list，并由一次 `end_frame()` 提交。准备结果在 execute 返回后离开作用域；成功录制命令所需 payload 由 command list 自身保活到 queue completion。

### 6. 测试证明行为边界，不新增测试专用生产入口

扩展现有 recording fake：分别记录 device creation、upload、begin/end render pass、binding 与 draw 顺序；验证所有 creation/upload 发生在 begin render pass 之前，execute 区间没有 device creation，并验证空 draw 仍 clear、prepare pass-level failure 不 begin、execute failure 会触发 discard/abort。

必要的私有阶段定向测试沿用现有 private-member access 方式，不把 prepared 类型或新测试 hook 暴露为生产 API。结构检查确认 `execute_base_pass` 的 RHI 输入只有 `RHIGraphicsCommandContext&`，且 implementation body 不引用 `RHIDevice` 或 scene/material/proxy source。

## Risks / Trade-offs

- [prepare 仍向 context 录制 upload，名称可能被误解为纯 CPU] → 在 contract 和注释中明确 prepare 是“draw 前资源物化阶段”，不是无副作用编译；强制与 execute 使用同一 recording。
- [prepared draw 强引用延长对象到函数末尾] → 生命周期只覆盖当前 frame recording；command list 成功录制后接管 GPU payload 保活，不建立跨帧 cache。
- [局部 shader/pipeline 创建失败按 batch 跳过，pass 仍可能只执行 clear] → 保持现有容错 contract；pass-level descriptor 或 context 命令失败仍中止整帧。
- [未来并行 pass 可能需要把准备与录制分配到不同任务] → 当前形状不承诺跨线程可移动；未来先按 TaskSystem 设计明确线程和 context ownership，再扩展，不在本 change 提前加入同步。
- [header 中出现私有 incomplete nested type] → 它表达真实阶段数据但不泄漏成员；若后续出现第二个业务 pass 的同形需求，再单独评估 renderscene-private 共用结构，当前不泛化。

## Migration Plan

1. 为现有 Base Pass 补充 operation-order、prepare failure、empty draw 与 execute failure 基线测试。
2. 引入私有 prepared pass/draw 值，先用单 vector 替换平行 vectors，保持原函数行为可构建。
3. 提取 `prepare_base_pass()`，迁移 descriptor validation、可变源解析、RHI 创建与 uniform materialization。
4. 提取 `execute_base_pass()`，收敛为只读取 prepared value 并调用 graphics context；更新 `render_frame()` 顺序。
5. 删除旧 `render_base_pass(RHIDevice&, RHIGraphicsCommandContext&, ...)` 混合入口，运行结构检查与受影响测试。
6. 由独立验证 sub-agent 完成 Windows configure、`Toy3dEditor` build、全量 CTest 与 Vulkan validation smoke。

这是内部调用链迁移，不保留新旧双轨入口。若回归无法在当前批次修复，整体回退 prepare/execute 提取并恢复单一旧函数；不得留下只被部分调用的 prepared 路径。
