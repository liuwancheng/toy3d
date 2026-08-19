# Toy3d 渲染引擎基础架构施工进度

## 1. 文档职责

本文是 `rendering-engine-foundation-design.md` 的施工台账，记录当前可复现基线、工作包状态、
验证证据、阻塞项和下一步。架构 contract、长期边界与第一里程碑验收条件仍以设计文档为准；
本文不得用施工便利改变已确认设计。

每个工作包只在实现、独立验证与主 agent 最终复查全部完成后标记为“完成”。对话上下文、
尚未提交的代码或仅有单元测试均不作为长期完成证据。

## 2. 当前可复现基线

- 基线 commit：`2bd29f4`（`更新 Shader 系统加载架构设计`）。
- 当前分支：`main`。
- 工作区状态：存在尚未提交的 Binding aggregation 代码、测试与设计，以及 Foundation
  批次 1 的路线收敛文档；不得在其上继续叠加 Renderer 新功能。
- 最近已确认的仓库能力：以基线 commit 和其历史构建记录为准；当前工作区验证结果见
  “当前工作包”。

## 3. 工作包状态

| 编号 | 名称 | 状态 | 完成证据 |
|---|---|---|---|
| BASE-0 | 当前工作区基线收拢 | 完成 | VS 2022/x64 重新配置、Debug 构建、CTest 8/8 与主 agent 复查 |
| BIND-AGG | 五逻辑 Binding Group 的 Vulkan physical set 聚合 | 自动验证通过，运行验收未完成 | 构建与 CTest 通过；Editor 正常关闭崩溃 |
| FND-1 | Foundation 文档与 RDG 路线收敛 | 完成 | 术语、链接、Material 与 RDG 路线一致性检查通过 |
| BASE-1 | Windows Editor shutdown 崩溃诊断与修复 | 未开始 | — |
| FND-2A | Renderer format contract 与 capability 验证 | 未开始 | — |

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
  `0xC0000005`，因此 Editor 冒烟整体失败，BIND-AGG 尚不能宣称完整运行验收通过。

只读诊断将 fault offset 定位到 `std::map<int, KeyCode>::_Find_lower_bound`。证据指向
`win32_input.cpp` 的静态 `win2keycode` 与 `Win32Window::~Win32Window -> DestroyWindow`
关闭消息回调之间可能存在静态析构顺序 use-after-destruction。该判断尚无 debugger 调用栈
最终确认，必须作为 BASE-1 的待验证假设，不能写成已确认根因。

## 5. 已知但不在当前工作包解决

- 公共 RHI owner identity 尚未完成；同 backend 类型跨 device 的对象混用仍是既有 P1。
- Global/View constant buffer 的具体字段属于后续 Renderer parameter contract，不在
  Binding aggregation 中定义。
- Vulkan physical packet 当前是 recording-local 生命周期；持久 descriptor cache、bindless、
  descriptor indexing 和 update-after-bind 均后置。
- D3D11、D3D12、Android 与 macOS 不属于第一里程碑实际运行验收，但新增公共 contract
  必须持续保持可实现性。

## 6. 建议提交拆分

实际提交前必须根据最终 diff 再核对文件归属，预期拆分为：

1. `完善 RHI Binding Group 聚合`：Binding aggregation 设计、公共 RHI、Vulkan backend、
   test pass、Shader 测试资产和对应自动测试；
2. `收敛渲染基础架构执行路线`：Foundation 总设计，以及 RHI requirement/current review、
   RHI design、Shader design 中与显式 Renderer、Material、TaskSystem 和 RDG 顺序相关的同步；
3. 本施工台账随第 2 个提交进入仓库。

若同一文件同时包含两个提交的内容，提交前应按语义拆分 patch；不得用重写文件或丢弃改动
来获得表面整洁。

## 7. 下一工作包

先执行 `BASE-1`，确认并修复 Windows Editor 正常关闭崩溃：

- 使用 debugger 调用栈确认 fault 是否来自静态 `win2keycode` 生命周期；
- 盘点 Win32 window/input 的既有所有权和消息分发，不新增重复平台基础设施；
- 修复后要求 `WM_CLOSE` 正常退出为 0、无残留进程，并保留原有输入映射行为；
- 不修改 RHI、Renderer、GameScene 或 RenderScene contract。

BASE-1 完成并将当前两个交付单元形成清晰提交后，进入 `FND-2A`：

- 验证 `R16G16B16A16Float` 的 `RenderTarget | ShaderResource` capability；
- 验证 `D24UNormS8UInt` 的 `DepthStencil` 与 depth-only sampled view contract；
- 增加不依赖 RenderScene 的独立 RHI 测试；
- 不同时处理 viewport resize、recoverable status 或 `abort_frame()`，这些继续拆为后续工作包。

## 8. 每轮交接规则

每轮开始读取 `AGENTS.md`、Foundation 总设计、本台账及当前工作包直接依赖的局部设计；
每轮结束记录：

- 实际修改文件与关键设计决定；
- 执行过的精确配置、构建、测试和运行命令；
- 成功、失败、跳过及其原因；
- 未决问题和下一工作包；
- 对应 commit（提交完成后补录）。

代码修改必须由独立 sub-agent 使用 `verify-toy3d-build` 验证。主 agent 根据验证结果修复并
最终复查；一个工作包完成后停止，不自动进入下一个工作包。
