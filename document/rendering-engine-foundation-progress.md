# Toy3d 渲染引擎基础架构施工进度

## 1. 文档职责

本文是 `rendering-engine-foundation-design.md` 的施工台账，记录当前可复现基线、工作包状态、
验证证据、阻塞项和下一步。架构 contract、长期边界与第一里程碑验收条件仍以设计文档为准；
本文不得用施工便利改变已确认设计。

每个工作包只在实现、独立验证与主 agent 最终复查全部完成后标记为“完成”。对话上下文、
尚未提交的代码或仅有单元测试均不作为长期完成证据。

## 2. 当前可复现基线

- 基线 commit：`5fbf01c`（`完善 RHI viewport 可恢复状态`）。
- 当前分支：`main`。
- 工作区状态：仅包含已独立验证的 FND-2C 失败帧闭环、独立 RHI status 测试与对应
  文档同步；尚未提交。
- 最近已确认的仓库能力：以基线 commit 和其历史构建记录为准；当前工作区验证结果见
  “当前工作包”。

## 3. 工作包状态

| 编号 | 名称 | 状态 | 完成证据 |
|---|---|---|---|
| BASE-0 | 当前工作区基线收拢 | 完成 | VS 2022/x64 重新配置、Debug 构建、CTest 8/8 与主 agent 复查 |
| BIND-AGG | 五逻辑 Binding Group 的 Vulkan physical set 聚合 | 完成 | 自动测试与两轮 Editor/Vulkan draw/present 冒烟通过 |
| FND-1 | Foundation 文档与 RDG 路线收敛 | 完成 | 术语、链接、Material 与 RDG 路线一致性检查通过 |
| BASE-1 | Windows Editor shutdown 崩溃诊断与修复 | 完成 | Debug 构建、CTest 8/8、两轮 `WM_CLOSE` 退出码 0 |
| FND-2A | Renderer format contract 与 capability 验证 | 完成 | Debug 全量构建、CTest 9/9 与公共 RHI format contract 测试通过 |
| FND-2B | Viewport resize 与 recoverable status | 完成 | Debug 全量构建、CTest 10/10 与公共 viewport status 测试通过 |
| FND-2C | `abort_frame()` 完整失败帧语义 | 完成 | Debug 重新配置、Editor/测试构建、CTest 10/10 与 diff 检查通过 |

## 4. 已完成工作包：BASE-0

### 4.1 目标

- 保护并盘点所有现有未提交改动；
- 判断 Binding aggregation 是否形成公共 RHI、Vulkan backend、调用方和测试闭环；
- 核对 Foundation 批次 1 的术语、链接、Material 与 RDG 路线；
- 给出可独立审查的提交拆分，不开始新的 Renderer 功能；
- 由独立 sub-agent 验证所有现有代码修改。

### 4.2 非目标

- 不进入 RGBA16F、D24S8、format capability 或 viewport failure path 施工；
- 不新增 World、RenderScene、Render Thread、Forward Renderer 或 ImGui 实现；
- 不删除旧 `SceneRendering/test_pass` bring-up 路径；
- 不引入 RDG、临时 Pass Scheduler、TaskSystem 或新的通用线程设施；
- 不执行 Git 提交、历史改写或丢弃用户改动。

### 4.3 当前审计结论

- 旧 `bind_binding_set()` 已从当前源码调用链删除，调用方迁移到
  `bind_graphics_bindings()`；当前未形成新旧正式入口双轨。
- 公共 `RHIGraphicsBindings` 保持 Global、View、Pass、Material、Object 五个逻辑引用，
  未暴露 Vulkan descriptor set、D3D register space 或 D3D12 root parameter。
- Vulkan 在录制期将 Global+View 聚合为 physical set 0，其他逻辑组映射到 set 1..3；
  physical packet 与 logical binding sets 由 command list 保活到 frame completion。
- required group、layout compatibility 与 resource state 在 draw 前检查；未实现的 storage 和
  buffer-view 路径返回 `Unsupported`。
- D3D11、D3D12 与 `VulkanPortable v1` 的可实现性已记录在 Binding aggregation 设计中，
  但这些 backend 尚无实际运行验收。
- 当前独立公共测试覆盖 logical group 字段、layout compatibility、空快照，以及跨逻辑组
  target slot 复用；ShaderMap loader 与 Vulkan test pass 已加入 Global+View 路径。

### 4.4 独立验证证据

验证环境为 Windows、Visual Studio 17 2022、x64、Debug、`BUILD_TESTING=ON`。独立
sub-agent 实际执行并通过：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON`；
- `cmake --build build --config Debug --target Toy3dEditor`；
- `Toy3dRHIBindingTests`、`Toy3dShaderMapEntryLoaderTests` 及其余已登记测试目标构建；
- `ctest --test-dir build -C Debug --output-on-failure`，8/8 通过；
- `git diff --check`，无 whitespace error，只有既有 LF→CRLF 提示。

修正 Vulkan 1.1 runtime 下两处仍写作 “Vulkan 1.0” 的错误诊断后，增量复验再次通过：

- `cmake --build build --config Debug --target Toy3dEditor`；
- 构建 `Toy3dRHIBindingTests` 与 `Toy3dShaderMapEntryLoaderTests`；
- `ctest --test-dir build -C Debug --output-on-failure -R
  "Toy3dRuntime\.(RHIBinding|ShaderMapEntryLoader)"`，2/2 通过；
- `git diff --check`，无 whitespace error。

### 4.5 Vulkan 运行证据与失败项

- validation 开启时 Editor 成功创建并保持响应，日志新增
  `The default RHI is connected to the main window at 1280x720.`；
- 本轮新增 Vulkan warning/error 为 0，未重现旧日志中的 SPIR-V 1.3/Vulkan 1.0 与
  binding overlap 错误；
- 未执行截图、像素级画面核对或完整多帧 validation suite；
- 发送 `WM_CLOSE` 后进程以 `0xC000041D` 异常退出，Windows Event Log 同时记录底层
  `0xC0000005`；这是 BASE-0 时的失败基线，已由 BASE-1 修复并复验。

只读诊断将 fault offset 定位到 `std::map<int, KeyCode>::_Find_lower_bound`。证据指向
`win32_input.cpp` 的静态 `win2keycode` 与 `Win32Window::~Win32Window -> DestroyWindow`
关闭消息回调之间可能存在静态析构顺序 use-after-destruction。该判断尚无 debugger 调用栈
最终确认；BASE-1 随后通过生命周期修复与重复运行验收验证了该诊断方向。

## 5. 已完成工作包：BASE-1

### 5.1 修复边界

- `Engine::exit()` 在 RHI shutdown 后、Logger shutdown 前显式销毁 window 与 platform，
  不再把 native window 拖到全局 `g_engine` 的静态析构阶段；
- `Win32Window` 析构时先解除全局 window callback，再退出并销毁 platform input，最后
  `DestroyWindow()`，窗口销毁消息不能访问处于析构中的对象；
- Win32 key mapping 改为无动态析构的 `constexpr` 表，只在对应 keyboard/mouse message
  分支执行翻译；鼠标按钮不再误用 `wParam` 的状态位作为 virtual-key code；
- 未修改 RHI、Renderer、GameScene、RenderScene 或共享基础设施 contract。

### 5.2 独立验证证据

- VS 2022 x64、`BUILD_TESTING=ON` 重新配置成功；
- `Toy3dEditor` Debug 与全部已登记测试目标构建成功；
- `ctest --test-dir build -C Debug --output-on-failure`：8/8 通过；
- 两轮通过主窗口句柄投递 `WM_CLOSE`，退出码均为 0；
- 两轮均成功连接 RHI，无新增 Vulkan warning/error、Application Error/WER 或残留进程；
- `git diff --check` 通过，仅有既有 LF→CRLF 提示。

未覆盖系统关机/会话注销、macOS、Android、D3D11、D3D12 和视觉像素正确性。BASE-1
只证明当前 Windows/Vulkan Editor 的正常窗口关闭路径与 BIND-AGG draw/present 冒烟。

## 6. 已完成工作包：FND-2A

### 6.1 实现边界与关键决定

- 公共 validation 新增 texture format capability 组合检查，创建所请求的全部 usage 必须同时
  受支持，sample count 也必须匹配；缺少任一能力返回带诊断信息的 `Unsupported`。
- `R16G16B16A16Float` SceneColor contract 固定验证
  `RenderTarget | ShaderResource`，RTV 与 SRV 均使用 `Color` aspect。
- `D24UNormS8UInt` SceneDepth contract 固定验证
  `DepthStencil | ShaderResource`；DSV 使用 `DepthStencil` aspect，sampled view 保持同一公共
  format 并只选择 `Depth` aspect。公共层不暴露 D3D typeless/native view format。
- texture descriptor 现在拒绝 `None`、buffer-only usage、color format 的 `DepthStencil` usage
  以及 depth/stencil format 的 `RenderTarget` usage；view validation 统一拒绝不兼容的 format、
  aspect、view type 与 read-only flag 组合。
- Vulkan format capability 补齐 transfer usage 报告；texture 创建在分配前先执行公共组合检查，
  再通过 `vkGetPhysicalDeviceImageFormatProperties` 精确复核 native format、组合 image usage 与
  sample count，不支持路径不会进入 `vkCreateImage`。
- 新增 `Toy3dRHIFormatTests`，仅依赖公共 RHI descriptor/capability contract，不依赖
  RenderScene、窗口或旧 `test_pass`。

跨后端可实现性结论：

- Vulkan 使用同 format 的 attachment view 与 depth-aspect sampled view，不要求
  separate depth/stencil layouts；`D24UNormS8UInt` 在 `VulkanPortable v1` 中仍是可选格式，
  由 runtime capability 决定是否支持，不提升移动基线；
- D3D11 FL11_0 可在 backend 内使用 typeless depth resource，分别创建 DSV 与 depth-only SRV；
- D3D12 同样由 backend 管理 typeless resource 与 native DSV/SRV format，并保持公共 contract
  不变；
- D3D11、D3D12 与移动设备本轮只完成公共可实现性评估，未实现 backend 或运行验收。

### 6.2 修改文件

- 公共 RHI：`rhi_public_definitions.h`、`rhi_descriptors.h/.cpp`；
- Vulkan backend：`vulkan_device.cpp`；
- 独立测试与登记：`tests/rhi_format_tests.cpp`、`engine/runtime/CMakeLists.txt`；
- 长期 contract：`toy3d-rhi-requirements.md`、`rhi-design.md`；
- 施工台账：本文件。

未修改 viewport resize、recoverable status、`abort_frame()`、RenderScene、旧 `test_pass`、
World、Render Thread、Forward Renderer、RDG、Scheduler 或 TaskSystem。

### 6.3 独立验证证据

独立 sub-agent 使用 `verify-toy3d-build`，在 Windows、Visual Studio 17 2022、x64、Debug、
`BUILD_TESTING=ON` 环境实际执行：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON`；
- `cmake --build build --config Debug --target Toy3dEditor`；
- `cmake --build build --config Debug --target Toy3dRHIFormatTests`；
- `cmake --build build --config Debug`；
- `ctest --test-dir build -C Debug --output-on-failure`，9/9 通过，包含新增
  `Toy3dRuntime.RHIFormat`；
- `git diff --check`，无 whitespace error，仅有既有 LF→CRLF 提示。

验证前后 HEAD 均为 `ed47a6d8910a8397686ee406bc78e7fbbde231ac`，工作区改动清单未被
验证者改变。没有构建或测试失败。未覆盖 macOS、Android/移动 `VulkanPortable v1` 真机、
D3D11、D3D12、真实 GPU format capability 运行时探测及画面像素正确性。

对应提交为 `71187dc`（`完善 Renderer 格式能力验证`）。

## 7. 已完成工作包：FND-2B

### 7.1 实现边界与关键决定

- 公共 `rhi_is_recoverable_viewport_status()` 将 `NotReady`、`OutOfDate` 与 `Suboptimal`
  定义为 viewport API 的可恢复结果；成功、参数错误、`DeviceLost` 与 `BackendFailure` 不属于
  recoverable failure。
- `NotReady` 只表达零 extent、最小化等暂时无法开始 presentation frame 的状态；Vulkan
  surface capability 报告零 extent 时在 `vkCreateSwapchainKHR` 前返回该状态并保留后续重建请求。
- Vulkan 的 terminal presentation failure 现在保存并重复返回原始错误 code/message；submit
  或同步失败不再在后续 `begin_frame()` 中降格为 `NotReady`，避免 Engine 永久静默跳帧；
  acquire 后 queue 内部若错误返回 recoverable code，则 viewport 将其归一为带原始 message 的
  terminal `BackendFailure`。
- `vkQueuePresentKHR` 返回 `VK_SUBOPTIMAL_KHR` 时，本帧仍已完成并安排后续 swapchain 重建，
  同时向 caller 返回带诊断的 `Suboptimal`；`VK_ERROR_OUT_OF_DATE_KHR` 继续映射为
  `OutOfDate` 并保持 resize pending。
- Engine 对 viewport API 的三类 recoverable outcome 不记录 error；terminal status 仍记录原始
  诊断。未引入新的 renderer error framework，也未改变 frame completion contract。
- 新增 `Toy3dRHIViewportStatusTests`，仅验证公共 status 分类，不依赖 RenderScene、窗口、
  Vulkan surface 或旧 `test_pass`。

跨后端可实现性结论：

- Vulkan desktop 与 `VulkanPortable v1` 使用相同公共分类，原生 WSI 的 out-of-date、
  suboptimal 与零 extent 细节只存在于 backend；
- D3D11/D3D12 可将 DXGI occluded/暂时不可 present 状态映射为 `NotReady`，将显式 resize
  pending 映射为 `OutOfDate`，并保留 device removed/reset 等 terminal HRESULT；
- 公共 helper 不依赖 Vulkan/DXGI 类型。D3D11、D3D12 与移动设备本轮未实现或运行验收。

### 7.2 修改文件

- 公共 RHI：`rhi_viewport_context.h`；
- Vulkan backend：`vulkan_viewport_context.h/.cpp`；
- 当前 composition root caller：`engine.cpp`；
- 独立测试与登记：`tests/rhi_viewport_status_tests.cpp`、`engine/runtime/CMakeLists.txt`；
- 长期 contract：`toy3d-rhi-requirements.md`、`rhi-design.md`；
- 施工台账：本文件。

未修改 `abort_frame()` 的 submission、acquire semaphore 消费或 frame-slot 推进语义，也未修改
RenderScene、旧 `test_pass`、World、Render Thread、Forward Renderer、RDG、Scheduler 或
TaskSystem。

### 7.3 独立验证证据

独立 sub-agent 使用 `verify-toy3d-build`，在 Windows、Visual Studio 17 2022、x64、Debug、
`BUILD_TESTING=ON` 环境实际执行：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON`；
- `cmake --build build --config Debug --target Toy3dEditor`；
- `cmake --build build --config Debug --target Toy3dRHIViewportStatusTests`；
- `cmake --build build --config Debug`；
- `ctest --test-dir build -C Debug --output-on-failure`，10/10 通过，包含新增
  `Toy3dRuntime.RHIViewportStatus`；
- `ctest --test-dir build -C Debug -N`，确认登记 10 项测试；
- `git diff --check`，无 whitespace error，仅有既有 LF→CRLF 提示。

验证前后 HEAD 均为 `71187dcb4b64b81e0b4474fa64914363b1a11012`，工作区改动清单未被
验证者改变。没有构建或测试失败。未实际启动 Editor 触发窗口最小化、resize、真实 Vulkan
surface 的 `OutOfDate`/`Suboptimal`；未覆盖 D3D11、D3D12、移动设备、macOS/Linux 与
非 Debug 配置。

主 agent 补强 acquire 后 submit failure 归类和不可恢复 present failure 锁存后，独立验证者
再次执行并通过：

- `cmake --build build --config Debug --target Toy3dEditor`；
- `cmake --build build --config Debug --target Toy3dRHIViewportStatusTests`；
- `ctest --test-dir build -C Debug --output-on-failure`，10/10 通过；
- `git diff --check`，无 whitespace error。

## 8. 已完成工作包：FND-2C

### 8.1 实现边界与关键决定

- `vkAcquireNextImageKHR` 返回 `VK_ERROR_OUT_OF_DATE_KHR` 时不再在同一次 `begin_frame()` 中
  重建并重试；本次返回 `OutOfDate`，下一次调用在干净边界重建，避免复用已销毁 frame slot 的
  semaphore 引用。
- acquire 已成功后，最小 present transition 的 command-buffer begin/end、fence reset、queue
  submit 或 abort presentation 前置步骤任一失败都会锁存 terminal status；底层若给出 recoverable
  viewport code，则统一提升为 `BackendFailure`，后续 `begin_frame()` 重复返回原诊断。
- `end_frame()` 在 native submit 前拒绝重复 command list，使 post-submit state publication 保持
  不失败前提；若该 CPU invariant 仍异常，已经提交的帧仍尝试 present 消费 `render_finished`，随后
  锁存 terminal failure。
- validation/recording failure 仍不提交已丢弃业务 command list；`abort_frame()` 只提交 present
  transition command buffer，成功 submit 后才发布 backbuffer committed `Present` state，并推进
  frame slot。submit 失败不发布 committed state。
- 公共测试扩展了 acquire 后未完成帧的 status normalization：`NotReady`、`OutOfDate`、
  `Suboptimal` 提升为 `BackendFailure`，既有 `DeviceLost` code/message 原样保留。

### 8.2 独立验证证据

独立 sub-agent 使用 `verify-toy3d-build`，在 Windows、Visual Studio 17 2022、x64、Debug、
`BUILD_TESTING=ON` 环境实际执行：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON`；
- `cmake --build build --config Debug --target Toy3dEditor`；
- `cmake --build build --config Debug --target Toy3dRHIViewportStatusTests`；
- `ctest --test-dir build -C Debug --output-on-failure`，10/10 通过；
- `git diff --check`，无 whitespace error，仅有既有 LF→CRLF 提示。

验证前后工作区均为相同 8 个已跟踪修改文件，无暂存或未跟踪项，验证者未修改实现和文档。
没有构建或测试失败。未启动 GUI Editor，未覆盖实际 Vulkan draw/present 与窗口关闭、D3D11、
D3D12、移动端、非 Windows 和非 Debug 配置。

主 agent 此前定向组合构建首次曾因 `Toy3dEditor.ilk` 瞬时占用产生 `LNK1104`；当时测试 target
已成功生成，确认无残留 Editor/MSBuild/link 进程后原命令重试通过。该瞬时失败未在独立验证中重现。

## 9. 已知但不在当前工作包解决

- 公共 RHI owner identity 尚未完成；同 backend 类型跨 device 的对象混用仍是既有 P1。
- Global/View constant buffer 的具体字段属于后续 Renderer parameter contract，不在
  Binding aggregation 中定义。
- Vulkan physical packet 当前是 recording-local 生命周期；持久 descriptor cache、bindless、
  descriptor indexing 和 update-after-bind 均后置。
- D3D11、D3D12、Android 与 macOS 不属于第一里程碑实际运行验收，但新增公共 contract
  必须持续保持可实现性。

## 10. 建议提交

FND-2C 的失败帧 contract、Vulkan 状态机修正、独立测试和文档互为一个
工作包闭环，建议作为
单一提交：

- `完善 RHI viewport 失败帧闭环`

提交前再次核对 diff，不包含构建产物、本机配置或后续工作包内容。

## 11. 下一工作包

FND-2C 独立验证、主 agent 复查并提交确认新基线后，批次 2 的 RHI 最小缺口闭环。下一轮再按
`rendering-engine-foundation-design.md` 单独拆分批次 3 的 GameScene 领域模型；不在本工作包同时开始
World、RenderScene、Render Thread 或 Forward Renderer。

本轮在 FND-2C 完成后停止，不进入批次 3。

## 12. 每轮交接规则

每轮开始读取 `AGENTS.md`、Foundation 总设计、本台账及当前工作包直接依赖的局部设计；
每轮结束记录：

- 实际修改文件与关键设计决定；
- 执行过的精确配置、构建、测试和运行命令；
- 成功、失败、跳过及其原因；
- 未决问题和下一工作包；
- 对应 commit（提交完成后补录）。

代码修改必须由独立 sub-agent 使用 `verify-toy3d-build` 验证。主 agent 根据验证结果修复并
最终复查；一个工作包完成后停止，不自动进入下一个工作包。
