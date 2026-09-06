## 1. Contract 测试与迁移盘点

- [x] 1.1 盘点 `RHIDevice` 全部 public 创建入口、`VulkanDevice` overrides、RHI object owner identity 和 renderscene 调用点，形成与 design 覆盖集合一致的迁移清单，并用 `rg` 确认没有遗漏的 public `create_*` virtual
- [x] 1.2 新增 backend-independent `Toy3dRHIDeviceFrontendTests` 测试 target 与最小 recording backend fake，验证测试可在不创建 Vulkan instance/window surface 的情况下构建和运行
- [x] 1.3 为全部创建类别添加“合法输入只调用一次 backend hook”的 contract cases，并运行 `Toy3dRHIDeviceFrontendTests` 确认当前缺口被测试表达

## 2. 通用生命周期 Admission

- [x] 2.1 将 pipeline 专用 creation scope/counter 泛化为全部 device 创建共享的 RAII admission，并用单元测试验证成功、frontend 失败和 backend 失败均释放 admission
- [x] 2.2 让 shutdown 拒绝新创建并等待已获 admission 的创建结束后再清 cache/backend state，使用可控阻塞 fake 验证 create/shutdown 竞争不会进入 teardown 后的 hook
- [x] 2.3 验证 `shutdown_after_device_lost()` 复用同一 admission 且不引入额外 native idle wait，并运行 frontend lifecycle tests

## 3. Resource 与 View 创建 Frontend

- [x] 3.1 将 buffer/texture 创建改为公共 NVI 与 protected backend hooks，统一 descriptor、initial data、capability/limits/format support 和 lifecycle 检查，并用 fake 断言失败不进入 backend
- [x] 3.2 将 buffer view/texture view 创建改为公共 NVI 与 protected backend hooks，统一 subresource/range/format validation 和 source resource device identity，并测试 cross-device view 在 native 调用前失败
- [x] 3.3 校验 resource/view 成功结果属于当前 device，测试错误 owner 结果返回可诊断 backend contract failure

## 4. Shader、Binding、Sampler、Pipeline 与 Fence Frontend

- [x] 4.1 将现有 shader、binding layout 与 graphics pipeline NVI 接入通用 admission，保留 pipeline canonical cache/single-flight 行为，并运行现有 binding/pipeline 相关测试确认无回归
- [x] 4.2 将 sampler 与 binding set 创建改为公共 NVI 与 protected hooks，统一 descriptor/capability/limits 以及 layout、view、resource、sampler device identity 检查，并覆盖混合 device binding set 失败测试
- [x] 4.3 将 GPU fence 创建改为公共 NVI 与 protected hook，验证空/非法输入、lifecycle 状态、unsupported backend 和成功 owner identity
- [x] 4.4 为 backend 返回的 shader、binding layout/set、sampler、pipeline 与 fence 统一执行结果 owner 检查，并运行 `Toy3dRHIDeviceFrontendTests`

## 5. Viewport 与 Command Context Acquisition

- [x] 5.1 将 viewport context 创建改为公共 NVI 与 protected hook，统一 surface/viewport descriptor、lifecycle 与 capability 检查，并验证 presentation 仍由 viewport/frame objects 拥有
- [x] 5.2 将 device-level graphics command context 创建改为公共 NVI 与 protected hook，验证未初始化、shutdown、device-lost、unsupported 和成功路径
- [x] 5.3 核对 frame-local context 仍由 `RHIFrameContext` 提供，Base Pass 的 GPU 命令仍只录制到显式传入的 `RHIGraphicsCommandContext&`；允许当前组合式准备/录制函数保留 `RHIDevice&` 创建 pipeline/binding，但用调用链静态检查确认没有 pass 通过 device 隐式创建 context、submit、present 或 wait

## 6. Vulkan Backend 迁移

- [x] 6.1 将 `VulkanDevice` 全部 public 创建 overrides 迁移为 protected `*_impl()`，保持原 native 创建、错误转换与对象所有权逻辑，并用 `rg` 确认不存在旧 public override 或兼容 wrapper
- [x] 6.2 删除已由公共 frontend 覆盖的 Vulkan 策略性重复 validation，同时保留 Vulkan API 前置条件、显式 enum mapping 和原生失败检查，并运行 Vulkan 定向单元测试
- [x] 6.3 核对 `VulkanDevice` 仍作为 memory/upload/deferred-deletion/queue 等 backend 子系统的 composition root，且本 change 未迁移 binding packet materialization 或改变 swapchain/frame/submit 行为

## 7. 文档与跨后端审查

- [x] 7.1 更新 `document/rhi-design.md`，登记 RHIDevice God facade、NVI/backend hook、统一 admission、owner identity 与 device/context/queue/viewport/pass 边界，并检查 `document/index.md` Active 入口仍有效
- [x] 7.2 按 Vulkan、D3D12、D3D11 FL11_0 与 VulkanPortable v1 逐项审查全部 frontend checks 和 unsupported 路径，确认公共头文件无 native 类型且上层无 backend/platform 判断
- [x] 7.3 对照 UE4.27 `FDynamicRHI/IRHICommandContext` 与 Flax `GPUDevice/GPUContext` 复核职责划分，确认未引入 `GDynamicRHI`、`GPUDevice::Instance`、device 级 Draw begin/end 或新的 service locator

## 8. 构建与验收

- [x] 8.1 使用推荐 Windows CMake 配置重新生成工程并构建 `Toy3dRHIDeviceFrontendTests`、受影响 RHI/Vulkan tests 与 `Toy3dEditor`，确认无编译或链接错误
- [x] 8.2 运行 `ctest --test-dir build -C Debug --output-on-failure`，确认 frontend、binding、resource-state、viewport、Vulkan swapchain 与 renderer 回归测试全部通过
- [x] 8.3 在 validation layer 下执行 bootstrap resource 创建/上传、正常多帧、resize/minimize/restore 与 shutdown smoke，确认无 validation error 且 frame/presentation 行为未改变
- [x] 8.4 由独立 sub-agent 使用 `verify-toy3d-build` 复核配置、构建、CTest、Vulkan smoke、旧 override/全局入口残留和文档一致性；修复其发现的问题后再完成 change
