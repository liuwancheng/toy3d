## Why

当前 `ForwardSceneRenderer::render_base_pass()` 同时通过 `RHIDevice` 创建 shader、pipeline、binding，通过 `RHIGraphicsCommandContext` 录制 uniform upload 和 draw 命令，使 pass 的准备策略与执行边界糅合在同一函数中。现在 `RHIDevice` frontend 与 Vulkan backend 职责已经收敛，应进一步让业务 pass 的执行阶段只消费显式 graphics context，为后续 pass 级独立录制保留清晰边界。

## What Changes

- 将 Forward Base Pass 拆为显式 prepare 与 execute 两个阶段：prepare 解析可见 `MeshBatch`、创建或查询 RHI shader/pipeline/binding，并把执行所需的强引用和值状态固化为当前帧私有数据；execute 只接收 `RHIGraphicsCommandContext&` 并录制 render pass、dynamic state、binding 与 draw。
- prepare 可显式接收当前 `RHIDevice` 与同一个 command context，用于 device 创建入口及 frame-local uniform upload；它不得创建或 finish context、submit、present 或 wait。
- execute 不得接收、保存或反向取得 `RHIDevice`、viewport、queue、frame context、RenderScene、Material/Proxy 或其他可变准备源；失败继续由外层 frame orchestration 触发 discard 与 `abort_frame()`。
- 保持第一阶段单 viewport、单 graphics context、单 immutable command list 的提交模型，并保持现有逐 batch 可诊断跳过、render-pass begin/end 和资源生命周期语义。
- 不建立公共 Pass Scheduler、RDG、通用 command packet、跨帧 prepared cache 或第二套 RHI frontend；本 change 只收敛现有 Forward Base Pass 的消费边界。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `game-render-framework/view-render-flow`: 明确 Forward Base Pass 的 prepare/execute 分界、准备结果的帧内所有权与强引用，以及 execute 仅消费 graphics context 和不可变准备数据的 contract。

## Impact

- 主要影响 `engine/runtime/renderscene/view/forward_scene_renderer.*` 及其 Renderer/RenderResource 相关测试。
- 公共 `engine/runtime/drivers/rhi/` 接口、Vulkan backend、frame submit/presentation contract 和 Shader/Material 资产格式保持不变。
- 增加 pass prepare/execute 顺序、执行期无 device 创建以及准备失败不进入 render pass 的结构与行为测试。
