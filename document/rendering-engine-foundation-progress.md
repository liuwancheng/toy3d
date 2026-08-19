# Toy3d 渲染引擎基础架构施工进度

## 1. 文档职责

本文是 `rendering-engine-foundation-design.md` 的施工台账，记录当前可复现基线、工作包状态、
验证证据、阻塞项和下一步。架构 contract、长期边界与第一里程碑验收条件仍以设计文档为准；
本文不得用施工便利改变已确认设计。

每个工作包只在实现、独立验证与主 agent 最终复查全部完成后标记为“完成”。对话上下文、
尚未提交的代码或仅有单元测试均不作为长期完成证据。

## 2. 当前可复现基线

- FND-5D1 施工起点 commit：`6b6f101`（`串联 Render Resource 与 RenderScene 基础`）。
- 当前分支：`main`。
- 当前工作包：FND-5D1 Mesh RHI resource、事务式随帧上传、失败重试、自动测试与本节文档同步；
  主 agent 与独立 sub-agent 验证通过。
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
| FND-3A | GameScene hierarchy 与 Transform | 完成 | Editor/GameScene 构建、CTest 11/11 与 hierarchy/transform 测试通过 |
| FND-3B | GameScene 可渲染 Component 与资产引用 | 完成 | Editor/GameScene 构建、CTest 11/11 与纯 CPU 资产 contract 测试通过 |
| FND-3C | Render 注册、dirty 合并与 owned-value batch | 完成 | Editor/GameScene 构建、CTest 11/11 与同步协议测试通过 |
| FND-4A | RenderFramePacket、completion 与 bounded queue | 完成 | VS 2022/x64/Vulkan 配置、Editor/定向构建、CTest 12/12 与 transport 测试通过 |
| FND-4B | Render frame dispatch、lag 与 single-thread fallback | 完成 | VS 2022/x64/Vulkan 配置、Editor/定向构建、CTest 12/12 与 transport 重复测试通过 |
| FND-5A | 持久 RenderScene 与增量 Apply | 完成 | Editor/定向构建、CTest 13/13 与 RenderScene contract 测试通过 |
| FND-5B | Render Resource version、placeholder 与 cache Apply | 完成 | Editor/定向构建、CTest 14/14 与 resource cache contract 测试通过 |
| FND-5C | Resource collection、Scene frame processor 与 Prepare resolve | 完成 | Editor/定向构建、CTest 15/15 与 pipeline contract 测试通过 |
| FND-5D1 | Mesh RHI resource 与事务式随帧上传 | 完成 | Editor/定向构建、CTest 16/16 与 upload transaction contract 测试通过 |

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

## 13. FND-3A：GameScene hierarchy 与 Transform 基础

本工作包开始批次 3，但只实现可独立验证的第一部分：

- 新增进程生命周期内单调分配的强类型 64 位 Render ID contract，`0` 保持 invalid，
  `PrimitiveId`、`LightId`、`RenderSceneId`、`ViewportId`、`SceneOutputId` 与资源 ID
  在类型系统中隔离；
- 新增由 `World` 持有的 `Actor`，以及由 `Actor` 独占持有的 `SceneComponent`；首个组件默认成为
  root，也可在同一 Actor 所有权内显式替换；
- 新增同 World attachment、cycle 检查、`KeepRelative` 与 `KeepWorld`；跨 World attachment、
  cycle、奇异父变换及无法无损表示为正缩放 TRS 的 shear 均记录错误日志并返回 `false`；
- Transform 使用 `translation * rotation * scale` 与 column-vector 约定，只接受有限值、非零
  quaternion 和每轴大于 `EPSILON` 的正缩放；父变换修改会递归标记所有后代 dirty，
  `World::update_transforms()` 按依赖更新最终 world matrix；
- 新增 `Toy3dGameSceneTests`，覆盖强类型 ID invalid/非复用、hierarchy 组合、dirty 传播、cycle、
  `KeepWorld` attach/detach、跨 World、非法 scale 与 root ownership。

本工作包没有开始 Mesh、Camera、Light、Material、RenderScene、Render Thread、Forward Renderer、
RDG、Scheduler 或 TaskSystem，也没有迁移或删除旧 `test_pass`。

主 agent 定向预检：

- `cmake --build build --config Debug --target Toy3dGameSceneTests`，成功；
- `ctest --test-dir build -C Debug -R Toy3dRuntime.GameScene --output-on-failure`，1/1 通过；
- 一次沙箱内重复构建因 Windows SDK 用户目录访问被拒绝而未进入编译，改用获准的沙箱外同命令后成功；
- `git diff --check`，无 whitespace error，仅有既有 LF→CRLF 提示。

独立验证者在 Windows、Visual Studio 17 2022、x64、`BUILD_TESTING=ON`、
`TOY3D_ENABLE_VULKAN_RHI=ON` 下执行：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
  -DTOY3D_ENABLE_VULKAN_RHI=ON`，成功；
- `cmake --build build --config Debug --target Toy3dEditor`，成功；
- `cmake --build build --config Debug --target Toy3dGameSceneTests`，成功；
- `ctest --test-dir build -C Debug --output-on-failure`，11/11 通过；
- `git diff --check`，无 whitespace error，仅有既有 LF→CRLF 提示；
- 验证前后工作区状态一致，验证者未修改实现、测试或文档。

主复查随后仅调整 attachment commit 顺序：先扩容新 parent 的 `children_`，成功后再解除旧关系，
保证分配异常不会留下单边 hierarchy；该调整完成后再次执行定向构建/测试与独立复验。
最终独立复验中 `Toy3dGameSceneTests` 与 `Toy3dEditor` 均构建成功，全量 CTest 仍为 11/11
通过，验证前后工作区一致。

未覆盖 Release/RelWithDebInfo/MinSizeRel、macOS、Android、Linux、D3D11、D3D12、移动端 Vulkan
profile、Editor 实际启动及人工画面检查。

下一工作包建议为 FND-3B：在本 hierarchy 上增加 StaticMesh、Camera、Directional/Point/Spot
Light Component 与 Material/MaterialInstance 引用；dirty 分类与 batch 合并继续作为 FND-3C，
避免单轮同时引入全部跨线程 payload。

## 14. FND-3A 目录与失败模型修正

根据 FND-3A 复查，完成以下收敛：

- `World` 与 `Actor` 迁移到 `engine/runtime/gamescene/`；
- `SceneComponent` 与 `SceneTransform` 迁移到 `engine/runtime/gamescene/component/`，不再使用职责
  笼统的 `gamescene/scene/`；
- 删除 `SceneStatus`、`SceneErrorCode` 与 `scene_status.h`；GameScene 操作失败时通过现有
  `TOY_LOG_ERROR` 记录原因，并以 `bool` 向调用方表达成功或失败；
- 删除只为限制 `std::make_unique` 构造路径而引入的 `ActorCreationToken`，保留简单的
  `Actor(World&)` 构造与 `World::create_actor()` 正式创建入口；
- 更新 GameScene 测试 include 与断言，不再依赖独立错误对象。

主 agent 修正后预检：

- `cmake --build build --config Debug --target Toy3dGameSceneTests`，成功；文件迁移触发 CMake
  `CONFIGURE_DEPENDS` 自动重新生成；
- `ctest --test-dir build -C Debug -R Toy3dRuntime.GameScene --output-on-failure`，1/1 通过。

独立验证者在 Windows、Visual Studio 17 2022、x64、`BUILD_TESTING=ON`、
`TOY3D_ENABLE_VULKAN_RHI=ON` 下重新配置并验证：

- `cmake --build build --config Debug --target Toy3dEditor`，成功；
- `cmake --build build --config Debug --target Toy3dGameSceneTests`，成功；
- `ctest --test-dir build -C Debug --output-on-failure`，11/11 通过；
- 静态检查确认没有 `gamescene/scene/` 文件或旧 include，代码/CMake 中没有 `SceneStatus`、
  `SceneErrorCode`、`ActorCreationToken`、`scene_status` 或 `scene_state` 残留；
- `git diff --check` 无 whitespace error，仅有既有 LF→CRLF 提示；
- 验证前后工作区状态一致，验证者未修改实现、测试或文档。

未覆盖 Release 系列、非 Windows 平台、D3D11、D3D12、移动端 Vulkan profile 与 Editor 交互/画面
冒烟测试。

## 15. FND-3B：GameScene 可渲染 Component 与资产引用

本工作包继续批次 3，只建立 Game Thread 领域对象与纯 CPU 资产引用：

- `rendercore/geometry` 新增不可变 `StaticMesh` CPU contract，包含 position/normal/UV0 vertex、
  UInt16/UInt32 index data、`StaticMeshSection`、Material Slot 与由 vertex position 推导的 local bounds；
  创建边界拒绝空数据、越界 index、非法 Section、空 Material Slot 与非有限 vertex 数据；
- `rendercore/material` 新增正式 `Material → MaterialInstance` 引用骨架；`Material` 保存不可变
  ShaderMap identity、Phong shading model、`Opaque | Translucent` 与 `two_sided`，
  `MaterialInstance` 强持有 `Material` 并预留单调 revision；typed parameter override 留给 Material
  参数工作包，不在 Component 内硬编码 Phong 参数；
- 新增 `StaticMeshComponent`，持有 `StaticMeshRef`，按 slot 解析 mesh 默认
  `MaterialInstanceRef` 或 Component override；替换 mesh 时同步清理旧 slot override；
- 新增 `CameraComponent`，第一版只接受有限远 Perspective，默认 60 度、near 0.1 m、
  far 1000 m；setter 原子验证 `0 < FOV < 180` 与 `0 < near < far`，并为后续 infinite-far、
  Orthographic 与 Custom projection 保留 projection mode；
- 新增 `DirectionalLightComponent`、`PointLightComponent` 与 `SpotLightComponent`；公共 Light
  参数验证非负 linear RGB、非负无量纲 intensity，local light 验证 `range > 0`，Spot 验证
  `0 <= inner <= outer < 90 degrees`。Directional 与 Spot 继续继承 SceneComponent 的本地 `+Z`
  朝向约定，不在 GameScene 中实现 shader 衰减或 light selection；
- 扩展 `Toy3dGameSceneTests`，覆盖资产创建与失败、Material Slot/override、Camera 默认值和原子
  失败、三类 Light 的公共参数与 Point/Spot 专有约束。

本工作包没有实现 Material typed parameter、Texture/Sampler、Render dirty/batch 合并、world bounds
缓存、RenderScene Proxy、RenderResourceCache、SceneView、Forward Renderer、Render Thread、RDG、
Scheduler 或 TaskSystem；这些仍按 FND-3C 及后续批次分开施工。旧 bring-up `Camera` 和
`SceneRendering/test_pass` 本轮不迁移或删除。

主 agent 定向预检：

- 沙箱内首次 `cmake --build` 因 MSBuild 无权读取 Windows SDK 用户目录失败，未进入编译；
- 获准在沙箱外执行 `cmake --build build --config Debug --target Toy3dGameSceneTests`，成功，
  `CONFIGURE_DEPENDS` 自动登记全部新增源码；
- `ctest --test-dir build -C Debug -R Toy3dRuntime.GameScene --output-on-failure`，1/1 通过；
- `git diff --check` 无 whitespace error，仅有既有 LF→CRLF 提示；新增目录未出现 backend 类型、
  `MaterialTemplate`、`MaterialInterface` 或直接 `new/delete`。

独立验证者完整读取 `verify-toy3d-build` skill 后，在 Windows、Visual Studio 17 2022、x64、
Debug、`BUILD_TESTING=ON`、`TOY3D_ENABLE_VULKAN_RHI=ON` 下执行：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
  -DTOY3D_ENABLE_VULKAN_RHI=ON`，配置与生成成功；
- `cmake --build build --config Debug --target Toy3dEditor`，成功；
- `cmake --build build --config Debug --target Toy3dGameSceneTests`，成功；
- `ctest --test-dir build -C Debug --output-on-failure`，11/11 通过；
- `git diff --check` 退出码 0，仅有两个已跟踪文件的 LF→CRLF 提示；
- 定向静态检查确认新增公共头没有 `Vk*`、`ID3D11*`、`ID3D12*`、直接 `new/delete`、
  `MaterialTemplate` 或 `MaterialInterface`；
- 验证前后工作区状态逐项一致，验证者未修改实现、测试或文档。

未覆盖 Release 系列、macOS、Linux、Android、D3D11/D3D12 backend、移动端 Vulkan profile 与
GPU 运行画面；本工作包是纯 CPU GameScene/asset contract，不把本次结果描述为 Renderer 画面验收。

下一工作包保持为 FND-3C：稳定 Render ID 注册边界、三类 dirty、帧末 batch 合并与 world bounds
更新；本轮完成后停止，不进入 Render Frame Transport。

## 16. FND-3C：Render 注册、dirty 合并与 owned-value 更新批次

### 16.1 实现边界与关键决定

- 强类型 Render ID 移至 `rendercore/render_id.h`，作为 GameScene、RenderCore 资产与后续
  RenderScene 共用的渲染身份 contract；各 ID 类型独立单调分配，`0` 保持 invalid，计数耗尽后
  保持 invalid 而不回绕复用。
- `World` 在帧末 `collect_render_scene_updates()` 边界为具备 Mesh 的 `StaticMeshComponent` 和三类
  Light 自动分配稳定 ID。首次出现发送完整 `Add`；已注册对象只发送一条合并后的 `Update`；
  已注册 Actor 销毁发送 `Remove`；同帧创建后销毁且从未注册的对象不发送消息。
- dirty 明确分为 `Transform`、`State` 与 `DynamicData`。Transform hierarchy 的既有递归 dirty
  传播同时标记 Render transform；Mesh/Material override 标记 State；Light 参数标记 DynamicData，
  enabled 标记 State。一次收集后清除已消费 dirty，不产生空重复更新。
- `RenderSceneUpdateBatch` 只携带矩阵、bounds、Light 参数与强类型资源 ID 等 owned value，不捕获
  World、Actor、Component、MaterialInstance 或其他 Game Thread 对象指针。`StaticMesh` 与
  `MaterialInstance` 在资产创建时获得稳定 Render Resource ID；移动构造显式转移身份并清空源 ID，
  避免两个活对象同时发布同一资源身份。
- `StaticMeshComponent` 的 world bounds 在帧末基于最终 world transform 更新；算法按仿射矩阵绝对值
  扩展 local AABB，可覆盖 positive non-uniform scale、旋转以及 hierarchy 组合产生的剪切矩阵。

本工作包没有实现 Render Thread、RenderScene Apply、RenderResourceCache、资源上传、Material typed
parameter、SceneView、Forward Prepare、RDG、Scheduler 或 TaskSystem。资源 ID 只建立跨线程引用
contract，具体 revision/update 与 GPU resource 生命周期仍属于后续 FND-5。

### 16.2 测试与验证

主 agent 定向执行并通过：

- `cmake --build build --config Debug --target Toy3dGameSceneTests`；
- `ctest --test-dir build -C Debug -R Toy3dRuntime.GameScene --output-on-failure`，1/1 通过；
- `git diff --check`，无 whitespace error，仅有既有 LF→CRLF 提示。

`Toy3dGameSceneTests` 新增覆盖：首次注册完整 Add、资源 ID owned-value snapshot、三类 dirty 的单条
合并、帧末 world bounds、无变化空批次、dirty 后 Remove 覆盖，以及同帧创建/销毁不发消息。

独立 sub-agent 完整读取并使用 `verify-toy3d-build` 后，在 Windows、Visual Studio 17 2022、x64、
Debug、`BUILD_TESTING=ON`、`TOY3D_ENABLE_VULKAN_RHI=ON` 下执行并通过：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
  -DTOY3D_ENABLE_VULKAN_RHI=ON`；
- `cmake --build build --config Debug --target Toy3dEditor`；
- `cmake --build build --config Debug --target Toy3dGameSceneTests`；
- 收到主 agent 的资源 ID move 语义补强后再次构建 `Toy3dEditor`，并核对最新源文件已进入对象；
- `ctest --test-dir build -C Debug --output-on-failure`，11/11 通过；
- `git diff --check` 退出码 0，仅有既有 LF→CRLF 提示；未跟踪新头另行检查无行尾空白且均有
  `#pragma once`；旧 `gamescene/render_id.h` 引用为 0，GameScene/RenderScene 未发现 backend 类型。

验证前后工作区源码条目一致，验证者未修改或提交文件。未覆盖 macOS、Linux、Android、
D3D11/D3D12、非 Debug 配置与 GPU/窗口运行态；本工作包为纯 CPU GameScene/asset sync contract，
不把结果描述为 Renderer 画面验收。

下一工作包为 FND-4A：先设计并实现 `RenderFramePacket`、completion 与 bounded queue 的纯 CPU
contract；仍不在该包创建真实 RHI Render Thread 或进入 RenderScene Apply。

## 17. FND-4A：Render Frame Packet、Completion 与 Bounded Queue

### 17.1 实现边界与关键决定

- `rendercore/render_id.h` 新增强类型 `RenderFrameId`，与 Scene、Viewport 及资源身份保持类型隔离；
- `RenderFramePacket` 持有 frame identity、有限且非负的 timing、`RenderSceneUpdateBatch` owned-value
  集合与共享 completion。packet 禁止复制、允许移动，避免一个 frame identity/completion 被意外复制成
  两次提交；
- 本包只聚合已经定型的 Scene 更新。尚未实现的 `RenderResourceUpdate`、`ViewportFrame` 与 ImGui
  payload 不建立占位 wrapper，待对应领域 contract 确认后再加入同一 packet；
- `RenderFrameCompletion` 使用 mutex/condition variable 实现一次性 first-terminal-result-wins 握手，
  区分 `Succeeded`、`Failed` 与 `Cancelled`，并保留 owned diagnostic；重复完成不能改写首个结果；
- `RenderFrameQueue` 固定只保存一个 queued packet。唯一渲染消费者取走该 packet 后形成
  一个 processing 加一个 queued 的设计上限；enqueue 在容量满时背压，不创建无界积压；
- `stop_accepting()` 停止新提交但允许已有 packet 优雅排空；`abort_pending()` 只取消仍由 queue 持有的
  packet，并在锁外完成 completion。已经 dequeue 的 processing packet 仍由渲染消费者负责完成，queue
  不伪造其结果；
- 本包没有创建 OS/通用 Thread、Event、Fence、TaskSystem 或全局 singleton，也没有接入 RHI、
  RenderScene Apply、resource upload、ViewFamily、one-frame lag policy 或 single-thread fallback。

### 17.2 测试与验证

`Toy3dRenderFrameTransportTests` 覆盖：

- completion pending/wait、成功、失败、诊断保留与重复完成 first-wins；
- packet owned Scene batch 与 FIFO 移交；
- 一个 queued packet 的 bounded backpressure，以及 dequeue 后释放容量；
- graceful stop 排空、停止后提交必达 cancellation、abort pending cancellation；
- invalid frame identity/NaN timing 拒绝且 completion 必达。

主 agent 定向构建与测试通过。一次沙箱内增量构建因 Windows SDK 用户目录访问被拒绝而未进入编译，
获准在沙箱外执行相同构建后成功；定向 CTest 1/1 通过。新增文件无行尾空白，公共 transport 头未发现
Vulkan、D3D11 或 D3D12 类型。

独立 sub-agent 完整读取并使用 `verify-toy3d-build` 后，在 Windows、Visual Studio 17 2022、x64、
Debug、`BUILD_TESTING=ON`、`TOY3D_ENABLE_VULKAN_RHI=ON` 下执行并通过：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
  -DTOY3D_ENABLE_VULKAN_RHI=ON`；
- `cmake --build build --config Debug --target Toy3dEditor`；
- `cmake --build build --config Debug --target Toy3dRenderFrameTransportTests`；
- `cmake --build build --config Debug --target Toy3dGameSceneTests`；
- `ctest --test-dir build -C Debug --output-on-failure`，12/12 通过；
- `git diff --check` 无 whitespace error，仅有两个既有 tracked 文件的 LF→CRLF 提示。

验证者确认最新 move-only ownership 收敛已进入构建对象，验证前后 source worktree 状态集合一致，未修改
实现、测试或文档。未覆盖 D3D11/D3D12、macOS、Android、Vulkan 运行画面、TSAN 或压力测试；本包是
纯 CPU transport contract，不把构建结果描述为 Render Thread 或 Renderer 运行验收。

下一工作包为 FND-4B：在本 transport contract 上实现帧派发生命周期、one-frame lag 与强制同步的
single-thread fallback，先以 fake processor 覆盖成功、处理失败、flush 和 shutdown completion 必达；
仍不在该包进入真实 RHI 初始化或 RenderScene Apply。

## 18. FND-4B：Render Frame Dispatch、Lag 与 Single-Thread Fallback

### 18.1 实现边界与关键决定

- 新增 `RenderFrameDispatcher` 作为 Main Thread 提交帧和控制渲染生命周期的唯一入口；
  composition root 注入并
  转移 `RenderFrameProcessor` 独占所有权，processor 的 initialize、process、flush、shutdown 与销毁都在
  配置选择的同一渲染执行线程上执行；
- `RenderFrameDispatcherConfig::multithreaded=true` 时创建单一 Render Thread，并用同步 startup handshake 确认
  processor 初始化结果后才允许提交；关闭 multithreaded 时不创建线程，强制
  `one_frame_thread_lag=false`，但仍执行完全相同的 processor 阶段；
- lag 开启时提交 N 后返回 N-1 completion，第一帧只提交不等待；lag 关闭或 single-thread fallback 时
  submit 同步返回 N completion。`flush()` 等待并清空 lag slot，再由渲染执行线程执行 processor flush；
- `RenderFrameQueue` 保持一个 queued packet 的容量，只增加 control interrupt，用于 queue 为空时唤醒
  Render Thread 执行 flush；interrupt 不占用 frame 容量、不伪造 frame identity，也不越过已排队 packet；
- `RenderFrameExecutionOutcome` 显式区分 `Succeeded`、`FrameFailed` 与 `FatalRenderer`：普通
  processor 失败只完成
  当前 frame 为 `Failed`，dispatcher 可继续处理后续帧；显式 fatal 结果或 processor 未预期异常则把
  completion 标记为 `Fatal`、停止接收并取消 queue 仍持有的 packet。single-thread 路径同样转入
  terminal state，异常不会越过 dispatcher 边界传播到 Gameplay；
- shutdown 先停止新提交，再按 FIFO 排空已接受 packet，随后由渲染执行线程调用 processor shutdown 并
  销毁 processor，最后 join Render Thread；重复 shutdown 保持幂等，停止后的提交以 `Cancelled`
  completion 返回；
- 本包的 completion 仍只表示 CPU processing 与未来 submit/present 调用已经返回，不表示 GPU 执行完成。
  本包未接入 RHI 初始化、RenderScene Apply、GPU resource、ViewportFrame 或 Editor composition root。

### 18.2 测试与主验证

`Toy3dRenderFrameTransportTests` 在 FND-4A 覆盖基础上新增：

- threaded startup handshake，以及 initialize/process/flush/shutdown 全部在同一 Render Thread；
- lag 开启时首帧不等待、提交 N 返回 N-1，flush 完成最后一个 lagged frame；
- threaded + lag off 同步返回当前 frame 的处理失败，普通失败不终止 dispatcher；显式
  `FatalRenderer` 产生 `Fatal` completion 并停止 dispatcher；
- single-thread 强制关闭 lag，并在 Main Thread 同步执行相同 processor 生命周期；
- shutdown 排空 accepted packet、completion 必达、重复 shutdown，以及 shutdown 后提交取消。

主 agent 在 Windows、Visual Studio 17 2022、x64、Debug 下构建
`Toy3dRenderFrameTransportTests` 与 `Toy3dEditor` 成功；全量 CTest 12/12 通过。定向 transport test 使用
最新二进制先后执行 `--repeat until-fail:100`、`--repeat until-fail:25` 与 fatal contract 更新后的
`--repeat until-fail:50`，累计 175 次通过。一次沙箱内
增量构建因 Windows SDK 用户目录访问被拒绝而未进入编译；随后获准在沙箱外完成相同构建，未把此前
旧测试二进制的结果计入有效构建证据。

独立 sub-agent 完整读取并使用 `verify-toy3d-build` 后，在 Windows、Visual Studio 17 2022、x64、
Debug、`BUILD_TESTING=ON`、`TOY3D_ENABLE_VULKAN_RHI=ON` 下执行并通过：

- 重新配置 CMake；
- 构建 `Toy3dEditor`、`Toy3dRenderFrameTransportTests` 与相邻生产端
  `Toy3dGameSceneTests`；
- 全量 CTest 12/12 通过；
- transport test 使用 `--repeat until-fail:100` 连续通过；
- `git diff --check` 与未跟踪 `render_frame_dispatcher.cpp/.h` 的独立 whitespace 检查无 error，仅有
  LF→CRLF
  提示；
- 最新生成的 `Toy3dRuntime.vcxproj` 包含 `render_frame_dispatcher.cpp/.h`，对象、Runtime library 与
  测试 executable
  时间戳均晚于最新源码，排除旧二进制误测；
- 公共 transport/dispatcher 头未发现 Vulkan、D3D11、D3D12 或 DXGI 原生类型，也未发现新增 UE 风格
  `I/F/E/T` 类型前缀；
- 验证前后源码工作树状态集合完全一致，验证者未修改或提交文件。

根据命名复查，随后删除此前机制性命名，全面收敛为职责明确的 `RenderFrameDispatcher`：文件名、类型、
配置、execution result、变量、错误文本、测试与 foundation 文档均同步迁移，不再为渲染线程所有权建立
代码对象。主 agent 触发 CMake glob 重新生成，生成工程只登记 `render_frame_dispatcher.cpp/.h`；
重新构建 Transport 与 Editor 成功，全量 CTest 12/12 通过，Transport 连续 50 次通过。

独立复验重新配置 CMake，构建 Editor、Transport 与 GameScene，CTest 12/12 及 Transport 连续 100 次
通过；源码、文档、CMake 文本与重新生成的工程中旧标识符、旧文件名与旧术语均为零匹配，
公共头仍无 backend 类型或 UE 风格类型前缀。复用 build 目录存在不再被生成工程引用的历史孤立
旧 object 文件，不属于当前源码或构建输入。复验期间主 agent 正在润色 progress 文档，因此验证前后
文件集合一致但文档字节统计变化；验证者未修改文件，并已针对最终文档重新执行旧命名扫描与
`git diff --check`。

未覆盖 D3D11/D3D12、macOS、Android、移动端 Vulkan profile、实际 Editor 窗口与渲染画面。下一
工作包建议为 FND-5A：只实现持久 `RenderScene` 的 Primitive/Light Info、Proxy 与增量 Apply 协议纯 CPU
闭环，不在同包接入 `RenderResourceCache`、RHI upload 或 View prepare。

## 19. FND-5A：持久 RenderScene 与增量 Apply

### 19.1 实现边界与关键决定

- 新增持久 `RenderScene`，按强类型 `PrimitiveId` 与 `LightId` 管理 `PrimitiveSceneInfo / Proxy` 和
  `LightSceneInfo / Proxy`；`Info` 保存 RenderScene 身份与 Proxy，Proxy 持有 Render Thread 后续 Prepare
  所需的完整 owned-value snapshot，不回访 `World`、Actor、Component 或 MaterialInstance；
- `State` 更新以完整 snapshot 原子替换 Proxy；Primitive `Transform` 只更新 world transform 与 bounds，
  Light `Transform + DynamicData` 只更新对应字段并保留 Light type、enabled 等 State；当前没有 Primitive
  DynamicData payload contract，若收到该标记会明确拒绝，不以空操作返回成功；
- `RenderSceneApplyResult` 记录 added、updated、removed、rejected 数量与结构化诊断。scene identity 不匹配
  时整批拒绝；对象级 invalid ID、duplicate Add、unknown Update/Remove、同 batch 重复 ID、非法 dirty flags
  或 snapshot 只拒绝当前对象并继续处理后续对象，且被拒绝的更新不改变既有 Proxy；
- Apply 边界验证矩阵、bounds 与 Light 参数为有限值，验证 Mesh resource identity、Point/Spot range 与 Spot
  cone 约束；Light DynamicData 先与既有 type 合成候选值再验证，避免恶意 payload 通过伪造 type 破坏现有
  Proxy invariant；
- 将 `RenderDirtyFlags` 与 `RenderSceneUpdateBatch` 从 `gamescene/` 迁到 `rendercore/`。它们是 GameScene
  producer 与 RenderScene consumer 共享的跨线程 contract，迁移后 RenderScene 不再直接或间接依赖
  GameScene；旧 include 路径已删除并收敛；
- 本工作包没有接入真实 `RenderFrameProcessor`、RHI、RenderResourceCache、revision、placeholder、upload、
  SceneView、Forward Prepare、RDG、Scheduler 或 TaskSystem。旧 `SceneRendering/test_pass` bring-up 路径仍按
  总体删除条件保留。

### 19.2 测试与主验证

新增 `Toy3dRenderSceneTests`，覆盖 Primitive/Light Add 与持久查询、Transform/DynamicData 局部合并、
State 完整替换、Remove、错误更新原子拒绝、同批次错误后继续、duplicate Add、同 ID 重复、scene mismatch
以及非有限 Light payload。

主 agent 在 Windows、Visual Studio 17 2022、x64、Debug 下执行并通过：

- `cmake --build build --config Debug --target Toy3dRenderSceneTests Toy3dGameSceneTests`；
- `ctest --test-dir build -C Debug -R "Toy3dRuntime.(RenderScene|GameScene)" --output-on-failure`，2/2 通过；
- `git diff --check` 无 whitespace error，仅有既有 tracked 文件的 LF→CRLF 提示；
- 静态检查确认新增 RenderScene/RenderCore 公共 contract 不含 Vulkan、D3D11、D3D12、DXGI 类型、直接
  `new/delete` 或旧 `gamescene/render_dirty.h`、`gamescene/render_scene_update.h` include。

独立 sub-agent 完整读取并使用 `verify-toy3d-build` 后，在 Windows、Visual Studio 17 2022、x64、
Debug、`BUILD_TESTING=ON`、`TOY3D_ENABLE_VULKAN_RHI=ON` 下执行并通过：

- 重新配置 CMake；
- 构建 `Toy3dEditor`、`Toy3dRenderSceneTests`、`Toy3dGameSceneTests` 与
  `Toy3dRenderFrameTransportTests`；
- 全量 CTest 13/13 通过；主 agent 补强定向测试后，验证者按最新源码重新构建 RenderScene test 并重跑
  全量 CTest，可执行文件时间戳晚于最新测试源码；
- `git diff --check` 退出码 0，仅有既有 LF→CRLF 提示；公共 contract/backend 类型、旧 include 与
  RenderScene/RenderCore→GameScene 依赖扫描均为零匹配；
- 验证前后工作树文件集合一致；验证者未修改实现、测试或文档。

未覆盖 D3D11/D3D12、macOS、Android、移动端 Vulkan profile、实际 Editor 窗口与渲染画面；本工作包是
纯 CPU RenderScene mirror contract，不把构建结果描述为 Render Thread、GPU resource 或 Renderer 画面验收。

下一工作包建议为 FND-5B：先定义 `RenderResourceUpdate`、强类型 revision、不可变 CPU resource version、
placeholder 与 cache Apply/miss/release 的纯 CPU contract，再单独接入 frame-local RHI upload 和 GPU
资源生命周期；不在资源 contract 尚未稳定时同时进入 SceneView 或 Forward Prepare。

## 20. FND-5B：Render Resource Version、Placeholder 与 Cache Apply

### 20.1 实现边界与关键决定

- 新增强类型 `RenderResourceRevision`，`StaticMesh` 与 `MaterialInstance` 的 CPU 资产接口统一暴露该类型；
  revision 0 为 invalid，资源版本只接受有效 ID 与有效 revision；
- `RenderResourceUpdate` 是 Mesh、Material、Texture 三种强类型 update 的封闭 `std::variant`。每个 Update
  携带 `shared_ptr<const ...Version>`，Release 只携带强类型 ID；`RenderFramePacket` 新增有序
  `resource_updates`，继续保持 move-only owned-value packet contract；
- `MeshRenderResourceVersion` 持有 vertex/index/section CPU 数据，`MaterialRenderResourceVersion` 持有
  shader 与静态 render-state 描述，`TextureRenderResourceVersion` 当前明确为 RGBA8 CPU pixels 与
  Color/Linear/Normal semantic。本工作包不把 RHI buffer、texture、view、upload allocation 或 backend
  handle 放入这些跨线程版本；
- `RenderResourceCache` 只属于 Render Thread/单线程 fallback owner，不增加内部通用线程设施。Apply 只接受
  高于 latest 的 revision；同 revision 同内容计为幂等 unchanged，同 revision 异内容报
  `RevisionConflict`，旧 revision 报 `StaleRevision`，错误对象不阻止同 stream 后续对象；
- 更新 latest 时不原地修改旧 version；cache 和调用方通过 `shared_ptr<const ...Version>` 保持具体版本。
  Release 只删除 latest，已被未来 `PreparedRenderFrame` 持有的旧版本仍可继续存活；
- cache 构造时必须提供 Error Material、checkerboard、white 与 normal 四个有效 placeholder。Material miss
  返回 Error Material；Texture miss 按 semantic 选择 placeholder；Mesh miss 返回 `Missing`，由后续 Prepare
  跳过 Primitive 并诊断；
- 本工作包仍是纯 CPU contract：没有实现 Game 侧资源更新收集器、真实 `RenderFrameProcessor` Apply 编排、
  RHI resource 创建、frame-local upload/transition、PendingUpload、GPU completion 或 Forward Prepare。

### 20.2 测试与主验证

新增 `Toy3dRenderResourceCacheTests`，覆盖 placeholder 初始化、三类资源 Apply/Resolve、幂等 update、
same-revision conflict、stale revision、错误后继续、latest replacement 的旧版本保活、Release、重复
Release，以及 Material/Texture/Mesh 三类 miss policy。Transport 测试同步确认 resource update 与 scene update
在 move-only packet 中共同跨队列传递；GameScene 测试确认 StaticMesh/MaterialInstance 使用强类型初始 revision。

主 agent 在 Windows、Visual Studio 17 2022、x64、Debug、`BUILD_TESTING=ON`、
`TOY3D_ENABLE_VULKAN_RHI=ON` 下执行并通过：

- 重新配置 CMake；
- 构建 `Toy3dRenderResourceCacheTests`、`Toy3dRenderFrameTransportTests`、`Toy3dRenderSceneTests` 与
  `Toy3dGameSceneTests`；
- 定向 CTest 4/4 通过，全量 CTest 14/14 通过；
- `git diff --check` 无 whitespace error，仅有既有 tracked 文件的 LF→CRLF 提示；
- 新增 resource contract、cache 与测试未发现 Vulkan、D3D11、D3D12、DXGI 或原生 backend 类型。

独立 sub-agent 完整读取并使用 `verify-toy3d-build` 后，在 Windows、Visual Studio 17 2022、x64、
Debug、`BUILD_TESTING=ON`、`TOY3D_ENABLE_VULKAN_RHI=ON` 下执行并通过：

- 重新配置 CMake；
- 构建 `Toy3dEditor`、`Toy3dRenderResourceCacheTests`、`Toy3dRenderFrameTransportTests`、
  `Toy3dRenderSceneTests` 与 `Toy3dGameSceneTests`；
- 全量 CTest 14/14 通过，0 失败；
- `git diff --check` 退出码 0，仅有既有 tracked 文件的 LF→CRLF 提示；
- 新增 contract/cache 与相关公共头的 Vulkan、D3D11、D3D12、DXGI、native backend 类型扫描为零匹配，
  `rendercore/`、`renderscene/resources/` 对 GameScene 的反向依赖扫描为零匹配；
- 验证前后 tracked modified 与 untracked 文件集合逐项一致，验证者未修改或提交文件。

本工作包不覆盖 D3D11/D3D12、macOS、Android、移动端 Vulkan profile、实际 Editor 窗口、RHI upload
或渲染画面；Windows 验证只覆盖 Vulkan RHI ON 的 Debug 配置。

下一工作包建议为 FND-5C：建立 Game 侧资源更新收集器与真实 `RenderFrameProcessor` 的
resource-before-scene Apply 顺序，让 cache version 能被持久 RenderScene 在 Prepare 边界 resolve；GPU
upload/transition 继续作为后续独立 vertical slice，避免在 processor 生命周期尚未闭环前耦合 RHI 状态。

## 21. FND-5C：Resource Collection、Scene Frame Processor 与 Prepare Resolve

### 21.1 实现边界与关键决定

- 新增 Main/Game Thread 所有的 `RenderResourceUpdateCollector`。调用方每个全局帧一次性提供全部活跃
  `World`，collector 跨 World 按强类型 ID 与 revision 去重 Mesh/Material；只有最后一个 World 引用消失后
  才发出无 version payload 的 `Release`，避免共享资产被单个 World 提前释放；
- `World` 只向 collector 枚举当前可渲染组件实际使用的 Mesh 和 resolved Material slot。Material override
  替代对应默认 slot 后只发布生效引用；collector 不把 Actor、Component 或裸指针放入跨线程 packet；
- 新增首个生产 `RenderSceneFrameProcessor`，由 dispatcher 选定的渲染执行线程独占。每帧固定先 Apply 完整
  `resource_updates`，再按 packet 顺序 Apply `scene_updates`，持久拥有 device-level cache 与多个
  `RenderScene`；resource/scene 的对象级诊断保存在 `RenderSceneFrameReport`，不把可跳过的 ContentError
  升级成整帧 fatal；
- 新增 `resolve_primitive_render_resources()` 作为 Forward Prepare 前的纯 CPU 解析边界。它从持久 Proxy 的
  resource identity snapshot 解析并强持有准确 immutable Mesh/Material version；Mesh miss 返回
  `MissingMesh`，Material miss 使用 Error Material，section 指向不存在的 Material slot 时返回
  `InvalidMaterialSlots`；
- processor 初始化验证 placeholder contract，flush 与 shutdown 遵守 dispatcher lifecycle，shutdown 清理
  持久 Scene；本包未接 composition root、RHI device/viewport、GPU buffer/texture、upload/transition、
  SceneView、Forward Pass、RDG、Scheduler 或 TaskSystem。

### 21.2 测试与主验证

新增 `Toy3dRenderFoundationPipelineTests`，覆盖：

- 两个 World 共享同一 Mesh/Material 时只发布一次，单 World 移除不释放，最后引用消失后才释放；
- revision 未变化不重复发布，Material override 只发布实际生效引用；
- 同一 packet 的 resource 与 scene update 经过真实 processor 后同时可解析；
- Prepare 强持有准确版本、Material miss 使用 Error Material、非法 section/material-slot 关系明确失败；
- 单对象 Apply 错误保留结构化诊断但不终止 frame，processor flush 与 shutdown lifecycle 闭环。

主 agent 在 Windows、Visual Studio 17 2022、x64、Debug、`BUILD_TESTING=ON`、
`TOY3D_ENABLE_VULKAN_RHI=ON` 下实际执行：

- 重新配置 CMake 成功；
- 构建 `Toy3dRenderFoundationPipelineTests`、`Toy3dGameSceneTests`、`Toy3dRenderSceneTests` 与
  `Toy3dRenderResourceCacheTests` 成功；
- 相关 CTest 4/4 通过；补强 override 与 invalid-slot 测试后，沙箱内增量构建因 Windows SDK 用户目录
  ACL 被拒绝，未进入编译；随后在沙箱外重跑相同 target 构建成功，并以最新二进制定向 CTest 1/1 通过；
- `git diff --check` 无 whitespace error，仅有既有 tracked 文件的 LF→CRLF 提示；新增公共 contract 未发现
  Vulkan、D3D11、D3D12、DXGI 或原生 backend 类型。

独立 sub-agent 完整读取并使用 `verify-toy3d-build` 后，在相同 Windows、VS 2022、x64、Debug 配置下
执行并通过：

- 重新配置 CMake，并确认生成器、架构、`BUILD_TESTING=ON` 与 `TOY3D_ENABLE_VULKAN_RHI=ON`；
- 构建 `Toy3dEditor`、`Toy3dRenderFoundationPipelineTests`、`Toy3dGameSceneTests`、
  `Toy3dRenderSceneTests`、`Toy3dRenderResourceCacheTests` 与 `Toy3dRenderFrameTransportTests`；
- 全量 CTest 15/15 通过，0 失败，包含新增 `Toy3dRuntime.RenderFoundationPipeline`；
- 生成工程包含 4 个新增生产 `.cpp`，对应 object 时间戳晚于源码，Runtime library 与 pipeline test
  executable 随后生成，排除旧二进制误测；
- `git diff --check` 通过，7 个 untracked 新文件另行扫描无行尾空白；新增边界无 backend 原生类型，
  `renderscene -> gamescene` 反向依赖扫描为零匹配；
- 验证前后均为相同 4 个 tracked 修改与 7 个 untracked 文件，HEAD 均为 `90359c8`，验证者未修改或提交。

未覆盖 macOS、Android、其他 UNIX、D3D11/D3D12 backend 运行态、真实 RHI upload 与渲染画面。本包是
CPU resource/scene Apply 与 Prepare resolve vertical slice，不把结果描述为 GPU Renderer 验收。

下一工作包建议为 FND-5D：在当前 CPU version/cache 与真实 processor 生命周期上，设计并实现
frame-local RHI resource creation、PendingUpload、upload-before-use transition 与失败帧重试；不在 GPU 资源
生命周期闭环前同时开始 SceneView/Forward Prepare，也不允许资源创建函数隐式 submit 或 wait idle。

## 22. FND-5D1：Mesh RHI Resource 与事务式随帧上传

### 22.1 实现边界与关键决定

- 新增不可变 `MeshRHIResource`，强持有准确 `MeshRenderResourceVersion`、vertex/index buffer、
  `RHIIndexFormat` 与 vertex stride；cache 只在该 RHI resource 对应当前 latest CPU version 时返回它，
  新 revision 或 Release 会移除 cache 强引用，已被 Prepared 输入持有的旧版本仍可存活至 RHI deferred
  deletion 安全回收；
- `record_pending_mesh_uploads()` 只在调用方已经开始的 frame-local graphics context 中录制，按 resource ID
  确定性排序，为每个 pending Mesh 创建 `VertexBuffer | CopyDestination` 与
  `IndexBuffer | CopyDestination` GPU-only buffer，记录
  `Common -> CopyDestination -> VertexBuffer/IndexBuffer`，并由 backend 在 `upload_buffer()` 返回前复制源数据；
- `RenderResourceUploadBatch` 是 submission 前后的显式事务边界。录制完成不修改 cache；调用方只有在包含
  这些命令的 viewport submission 成功后才调用 `commit_uploads()`。submit 前丢弃、recording failure 或
  revision 在 commit 前改变都不会发布 GPU 状态，下帧重新创建并重试；
- commit 先验证整批 CPU version 仍是 latest，再一次性发布，避免部分提交。资源创建继续传入空
  `initial_data`，本路径不调用 `submit()`、`wait_idle()` 或 device-level command context；
- 本包只闭合 Mesh vertex/index buffer。Texture RHI resource、placeholder/font bootstrap、真实 viewport
  orchestrator 接线、SceneView、Forward Prepare/Draw、Material binding 与画面验收留给后续独立工作包。

跨后端可实现性：Vulkan 由现有 frame-local staging 与 local state tracker 映射；D3D12 可映射 default buffer、
upload allocation 与 copy/transition；D3D11 可由 backend 将相同语义映射为 default buffer 和更新/copy 路径，
逻辑 transition 用于 hazard 验证；移动端 Vulkan 不新增 capability、descriptor set 或 profile 要求。

### 22.2 测试与主验证

新增 `Toy3dRenderResourceUploadTests`，使用公共 RHI fake 验证：

- vertex/index buffer descriptor、录制顺序、index format 与 vertex stride；
- record 后、commit 前不可 resolve，丢弃 batch 后下帧重新录制；
- commit 后相同 latest version 不重复上传；
- CPU revision 替换不破坏外部持有的旧 RHI resource，旧 batch 不能覆盖新 revision；
- 中途 upload recording failure 不发布部分状态，后续帧可重试；
- Release 同时移除 cache 所有的 latest CPU 与 RHI Mesh 引用。

主 agent 在 Windows、Visual Studio 17 2022、x64、Debug、`BUILD_TESTING=ON`、
`TOY3D_ENABLE_VULKAN_RHI=ON` 下完成 CMake 重新配置，构建 `Toy3dEditor`、
`Toy3dRenderResourceUploadTests`、`Toy3dRenderResourceCacheTests` 与
`Toy3dRenderFoundationPipelineTests`；相关 CTest 3/3 通过，`git diff --check` 无 whitespace error，
新增 production 上传边界未发现 Vulkan、D3D11、D3D12 或 DXGI 原生类型。

独立 sub-agent 完整读取并使用 `verify-toy3d-build` 后，在相同 Windows、VS 2022、x64、Debug 配置下
执行并通过：

- 重新配置 CMake，确认 VS 2022、x64、`BUILD_TESTING=ON` 与 `TOY3D_ENABLE_VULKAN_RHI=ON`；
- 构建 `Toy3dEditor`、`Toy3dRenderResourceUploadTests`、`Toy3dRenderResourceCacheTests`、
  `Toy3dRenderFoundationPipelineTests` 与 `Toy3dRenderFrameTransportTests`；
- 全量 CTest 16/16 通过，0 失败；直接运行 upload test 输出
  `Render resource upload checks passed.`；
- 生成工程已登记新增 production 与测试源码，Runtime library 和测试 executable 时间戳晚于输入源码，
  排除旧二进制误测；
- `git diff --check` 与 3 个 untracked 文件的独立 whitespace 检查通过；production 上传边界没有 backend
  原生类型，也没有 submit、wait、queue、finish-recording 或 device-level context 调用；
- 验证前后 HEAD 均为 `6b6f101`，4 个 tracked 修改与 3 个 untracked 文件集合一致，验证者未修改或提交。

未覆盖 D3D11/D3D12、macOS、Android、真实 Vulkan queue submission、validation layer 与渲染画面。本包只
证明公共 RHI 命令记录和 cache 发布事务；FND-5D2 应继续完成 Texture RHI resource 与 upload transaction，
之后再由 viewport orchestrator 把 upload batch commit 接到真实 submission 结果。
