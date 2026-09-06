## Context

见 `proposal.md` 的动机与 `specs/rhi-device-frontend/spec.md` 的行为 contract。当前 `RHIDevice` 已包含 shader、binding layout、graphics pipeline 和 shutdown 的部分 NVI，但 buffer、texture、view、sampler、binding set、fence、viewport 与 device-level context 仍由 `VulkanDevice` 直接公开 override；lifecycle admission 也只覆盖 pipeline 创建。`VulkanDevice` 因而同时承担根对象、公共策略、原生创建、cache 协作和内部子系统访问等职责。

UE4.27 的 `FDynamicRHI` 证明“一个后端根接口 + 独立 command context”可以稳定承载多 API；Flax 的 `GPUDevice + GPUContext` 也证明上层通常需要一个集中设备入口。但 UE 的 `GDynamicRHI`/全局包装层和 Flax 的 `GPUDevice::Instance`、device 级 `DrawBegin/DrawEnd` 都把访问便利性与全局生命周期耦合，不适合 Toy3d 已确立的 Renderer composition root、可替换依赖和 frame ownership。

## Goals / Non-Goals

**Goals:**

- 让 `RHIDevice` 成为窄而完整的 God facade：上层只需要一个显式根对象即可发现能力、创建对象并取得 queue/context/viewport。
- 让公共 frontend 成为 validation、capability、identity、lifecycle 和错误语义的唯一权威。
- 让 Vulkan 只覆盖 backend hooks，并为 D3D11/D3D12 提供可直接遵循的实现形状。
- 通过 backend-independent contract tests 锁定“失败不进入 backend”和 shutdown admission 行为。

**Non-Goals:**

- 不把 draw、dispatch、transition、render pass、submit、present 或 wait 移入 `RHIDevice`。
- 不引入全局 `g_rhi`、singleton、`RHIServices`、`RHIManager` 或第二层 facade。
- 不在本 change 拆分 `VulkanDevice.cpp`、迁移 `VulkanBindingPacket` materialization、重写 memory/upload/deferred-deletion 子系统。
- 不改变一帧一个 graphics list、viewport frame closure、queue committed state 或现有 pass orchestration。
- 不实现 D3D11/D3D12 backend；只保证公共 contract 可实现并用测试替身验证 frontend。

## Decisions

### 1. 采用显式 `RHIDevice` 根门面，不采用全局访问

Renderer 继续以 `std::unique_ptr<RHIDevice>` 拥有唯一 device，并向 bootstrap、RenderResource、SceneRenderTargets 和需要 prepare/cache 的代码显式传入引用。长期资源对象只记录非 owning device identity 用于 validation，不反向查找全局 device。

选择原因：这保留 Flax `GPUDevice` 的集中发现体验和 UE `FDynamicRHI` 的跨后端根边界，同时避免两者历史全局入口带来的测试隔离、重启和 teardown 次序问题。

备选方案：新增 `RHISystem/RHIServices` 聚合 device、queue、viewport。拒绝，因为它没有新增独立所有权或生命周期，只会把现有稳定名词再包一层。

### 2. 所有公共创建方法统一为 NVI

公共非 virtual 方法维持现有调用签名，并按统一骨架执行：

1. descriptor 与参数结构 validation；
2. 获取通用 creation admission，shutdown/terminal 时失败；
3. 检查 initialized 状态；
4. 检查 capability、limits、format support 与所有输入对象的 device identity；
5. 调用且只调用一次受保护的 `create_*_impl()`；
6. 检查成功结果的 owner identity，再发布给调用方。

覆盖集合为 `create_viewport_context`、`create_buffer`、`create_texture`、`create_buffer_view`、`create_texture_view`、`create_shader`、`create_binding_layout`、`create_sampler`、`create_binding_set`、`create_graphics_pipeline`、`create_gpu_fence` 和 `create_graphics_command_context`。`initialize`、capability queries 与 `graphics_queue()` 本轮不改成创建 NVI；`shutdown` 继续是已有 NVI。

backend protected hooks 仅做 native mapping/allocation 和后端特有失败转换，不重复决定跨 API contract。Vulkan override 统一改名为 `*_impl()`，原函数体按最小改动迁移。

备选方案：保留 public virtual 并要求每个 backend 手工调用公共 helper。拒绝，因为遗漏 helper 仍可编译，无法保证 D3D11/D3D12 与 Vulkan 行为一致。

### 3. 将 pipeline 专用 admission 泛化为 device creation admission

现有 `PipelineCreationScope`、`active_pipeline_creations` 和 `begin_pipeline_creation()` 泛化为私有 `CreationScope`、`active_creations` 与 `begin_creation()`。每个 NVI 在 backend hook 前获得 RAII scope，所有返回路径自动释放计数。shutdown 在设置 `shutting_down` 后等待 `active_creations == 0`，再执行 idle policy、清 cache 和 backend teardown。

第一阶段不新增复杂 lifecycle enum；沿用 backend 的 initialized/terminal 查询和既有 `shutdown_after_device_lost()`。若实现盘点发现 `is_initialized_impl()` 不能区分 ordinary not-ready 与 device-lost，则只增加表达稳定 terminal 诊断所需的最小公共/受保护查询，不把 Vulkan state 泄漏到公共层。

备选方案：仅给各 cache 加锁。拒绝，因为非 cache 创建仍可能与 native device teardown 竞争。

### 4. Validation 按对象类别组合，避免单个巨型函数

保留既有 `validate_*_desc()` 作为结构检查，并在 `rhi_device.cpp` 使用按创建类别命名的私有 helper 组合 capability 与 identity 检查。公共接口不新增 `Validator`、`Token` 或 `Key` 类型；只有确实跨多种对象复用且表达稳定规则的 helper 才进入公共头文件。

关键规则包括：

- `initial_data != nullptr` 继续按当前 contract 返回 `Unsupported`，资源上传仍走 command context；
- view 的 source resource、binding set 的 layout/resources/views/samplers、pipeline 的 shaders/layout 必须属于当前 device；
- texture/buffer/view/pipeline 请求的 format usage、sample count、size、alignment 和数量同时满足 capabilities/limits；
- viewport 校验 surface/desc，但 platform surface 本身不伪造 device ownership；
- backend 成功结果必须携带当前 device identity。

### 5. Command、queue、frame 和 pass 保持独立对象

上层依赖关系固定为：

```text
Renderer (owner)
    |
    +-- RHIDevice -------- create/query/acquire execution objects
    |      +-- RHIQueue ---------------- submit/completion/wait
    |      +-- RHIViewportContext ------- presentation + frame boundary
    |      +-- RHIGraphicsCommandContext  recording
    |
    +-- SceneRenderer / Pass
           +-- current combined prepare/record path may receive RHIDevice&
           +-- GPU commands use the supplied RHIGraphicsCommandContext& only
```

`ForwardSceneRenderer` 外层 frame orchestration 继续从 `RHIFrameContext` 创建当前 context；当前 `render_base_pass(RHIDevice&, RHIGraphicsCommandContext&, ...)` 在同一函数中完成 pipeline/binding 准备和 GPU 命令录制，因此保留显式 device 参数，但所有 transition、render pass、binding 和 draw 命令仍只写入传入的 context。它不得通过 device 取得 queue/viewport/context 或执行隐式 submit、present、wait。将准备与录制拆成不同阶段、让 execute 最终只接收 context，属于后续 `stabilize-render-pass-rhi-consumption` change。device-level context 只用于显式 bootstrap/flush 等无 viewport submission，finish 后由调用方显式提交 queue。

这吸收 UE/Flax 的 device/context 分工，但不复制 UE command-list replay/RHI thread，也不复制 Flax 将逐帧 begin/end 放在 device 的做法。

### 6. 用 recording backend fake 测试 frontend，不依赖 Vulkan 才能证明公共规则

增加最小测试 backend，记录每个 hook 的调用次数、输入和可控返回值，并创建带公共 owner identity 的轻量测试对象。测试覆盖每类创建至少一个合法路径，以及代表性的 invalid descriptor、unsupported capability、cross-device、backend failure、错误 owner、shutdown/new-create race。

Vulkan 定向测试只验证 override 迁移后真实创建路径仍工作；公共 contract 测试不要求 Vulkan SDK 或窗口 surface，便于未来 D3D backend 复用。

## Risks / Trade-offs

- [统一 admission 给所有创建增加一次 mutex 成本] → 创建不是 draw hot path；pipeline/cache 和资源创建正确性优先。后续只有 profiling 证明需要时才考虑 shared admission，不提前增加复杂同步。
- [frontend 与 Vulkan 现有 validation 重复] → 先迁移权威规则并保留 backend 原生前置条件检查；确认测试覆盖后删除语义重复的 backend policy，不删除 Vulkan API 必需检查。
- [一次改动覆盖创建方法较多，容易漏掉调用路径] → 以公共方法清单、Vulkan override 清单和 renderscene `create_*` 调用清单三向核对，逐类迁移并保持签名不变。
- [结果 owner identity 检查可能暴露旧对象未登记 owner] → 将其视为本 change 必须修复的 contract 缺口，禁止用 backend downcast 或跳过检查掩盖。
- [God facade 未来继续膨胀] → 新增方法必须属于 capability/query、长期对象创建或 execution-object acquisition；命令、submission、presentation 和 pass scheduling 明确禁止进入 device。

## Migration Plan

1. 先补齐 frontend contract tests 与测试 backend，使现有直派发路径的缺口可观察。
2. 泛化 creation admission，并逐类为 `RHIDevice` 增加 NVI 与受保护 hook；每一类先通过公共测试再迁移下一类。
3. 将 `VulkanDevice` public overrides 改为 protected `*_impl()`，最小迁移现有实现体并删除重复公共策略。
4. 核对 Renderer、SceneRenderTargets、RenderResource、Material 与 frame orchestration 调用点仍使用原公共签名，pass 执行边界不变。
5. 更新 Active RHI 设计和定向测试，完成构建、CTest 与 Vulkan smoke 后才勾选验证任务。

回滚时可按创建类别恢复 public virtual override；在全部类别迁移完成前不提交长期兼容 wrapper，也不保留两套正式入口。
