## Why

当前 `RHIDevice` 已经是 Renderer 显式持有的后端根入口，但公共创建路径只有 shader、binding layout 与 graphics pipeline 进入统一 frontend，其余 resource、view、sampler、binding set、viewport、GPU fence 与 device-level command context 仍直接虚派发到 Vulkan。结果是 validation、capability、device identity、初始化/关闭状态和错误语义分散在 backend，继续增加 D3D11/D3D12 会复制策略并放大行为差异。

## What Changes

- 将 `RHIDevice` 定型为 Renderer-owned、显式注入的唯一 RHI 根门面；上层通过它发现 capability/limits、创建长期对象并取得 queue/context/viewport，不新增全局 device 或 service locator。
- 让全部公共创建方法采用 non-virtual interface：公共 frontend 统一执行 descriptor validation、capability/limits、device ownership identity、lifecycle admission 与错误归类，再调用受保护的 backend `*_impl()`。
- 统一 shutdown 与并发创建的 admission，保证关闭开始后不再进入新的创建工作，并等待已经获准的创建结束后再释放 cache 和 backend state。
- 保持 `RHIGraphicsCommandContext` 为 draw-time 命令入口，保持 `RHIQueue`、`RHIViewportContext`、`RHIFrameContext` 和 immutable command list 的独立语义；现有 pass 准备阶段仍可显式使用 `RHIDevice` 创建 pipeline、binding 等长期对象，但不得通过 device 录制 draw、隐式 submit、present 或 wait。prepare/execute 彻底拆分留给后续 pass-consumption change。
- **BREAKING**：后端实现接口由覆盖公共 `create_*()` 改为覆盖受保护的 `create_*_impl()`；renderscene 可见的公共调用签名和现有 frame/presentation 行为保持不变。
- 本 change 不拆分 `VulkanDevice` 内部大文件，不迁移 descriptor materialization owner，不改变 pass 调度、资源状态提交、queue completion 或 swapchain 生命周期；这些分别留给后续 Vulkan decomposition 与 pass-consumption change。

## Capabilities

### New Capabilities

- `rhi-device-frontend`: 定义统一 device 根门面、公共创建 frontend、backend hook、生命周期 admission、device identity 与命令边界。

### Modified Capabilities

无。

## Impact

- 公共 RHI：`engine/runtime/drivers/rhi/rhi_device.*` 及各类 descriptor validation/helper。
- Vulkan backend：`VulkanDevice` 的 override 入口和创建实现命名；原生资源创建逻辑原则上只迁移入口，不改变算法。
- RenderScene：现有调用点用于回归确认，正常情况下无需改为新的 facade、manager 或全局入口。
- 后续后端：D3D11、D3D12 和 VulkanPortable v1 获得同一公共 validation、lifecycle 和错误 contract。
- 测试：补齐公共 frontend 的 backend-independent contract 测试，并保留 Vulkan 定向集成验证。
