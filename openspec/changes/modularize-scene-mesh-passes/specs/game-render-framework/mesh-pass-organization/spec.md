## Purpose

定义 Toy3d 具体 mesh pass 的稳定模块边界、draw list 与 draw command 数据 contract，以及 BasePass 当前录制闭环和未来 ShadowPass 扩展方式，避免把业务 pass 调度或后端物理 binding 泄漏到公共 RHI。

## Type Contracts

| 类型 | 性质与职责 | 所有权与线程 | 错误语义与复用理由 |
| --- | --- | --- | --- |
| `MeshDrawCommand` | 新增 frame-local value type；保存一次 indexed mesh draw 已解析的 graphics pipeline、vertex/index bindings、完整 logical `RHIGraphicsBindings` 引用快照、draw arguments 与 sort key，不保存 attachment 或准备源指针 | 由具体 mesh pass 在 logical RT 构造，归属对应 `MeshPassDrawList`；只在同一 recording 中消费，内部 RHI 强引用由 command list 继续保活到 queue completion | 无效 pipeline、binding 或 buffer 不得进入 command；不能复用 `MeshBatch`，因为后者是未解析的 scene/material draw input，也不能复用 RHI command list，因为它不表达业务 draw identity |
| `MeshPassDrawList` | 新增 frame-local value type；表示一个 render view 在一个具体 mesh pass 中的有序 draw command 集合，并保存该集合共用的 viewport/scissor；不保存 `RHIRenderPassDesc`、attachment、device、context、scene 或 backend packet | 由具体 pass 在 logical RT 构造并在同一 pass 调用内消费，不跨帧缓存；其 command 中的强引用可转入当前 RHI command list | 构造失败不得发布部分可执行 list；不能保留 `PreparedBasePass`，因为 draw list 是多个 mesh pass 可共享的稳定层级而非 BasePass prepare/execute wrapper |
| `BasePassInputs` | 新增 RT input value；以 non-owning view 引用观察当前 `ViewInfo` 集合，并以 RHI strong refs 保活 SceneColor/SceneDepth attachment views；不拥有 frame、queue、context 或完整 scene targets | 由 `ForwardSceneRenderer` 在 logical RT 调用期间按值构造；ViewInfo 引用目标必须覆盖 `render_base_pass(...)` 调用，attachment refs 按公共 RHI 引用语义持有 | 缺失 attachment、device 不匹配或 view 输入无效返回可诊断 RHI failure；不能让 BasePass直接持有 `SceneRenderTargets`，因为具体 pass 只应看到其声明的资源 |

## ADDED Requirements

### Requirement: 具体 mesh pass 使用独立模块
每个正式 mesh pass SHALL 位于 `engine/runtime/renderscene/pass/` 下的独立 `.h/.cpp` 模块，并通过明确的 `render_*_pass(...)` 入口接收调用方拥有的输入、`RHIDevice`、shader program cache 与 graphics context。第一阶段 MUST NOT 新增通用 `RenderPass` 基类、通用 Pass Scheduler、command packet hierarchy 或无真实复用依据的 `MeshPassProcessor` 基类；未来 RDG 仍属于 renderscene 并负责资源依赖与调度。

#### Scenario: 新增 ShadowPass
- **WHEN** Renderer 后续加入 ShadowPass
- **THEN** 它 MUST 作为 `pass/shadow_pass.*` 的具体模块接入，可复用已确认的 mesh draw contracts，但不得继承仅用于统一函数形状的 pass 基类

### Requirement: Mesh pass 数据层级稳定
`MeshPassDrawList` MUST 表示一个 render view 在一个具体 mesh pass 中的 frame-local draw list，`MeshDrawCommand` MUST 表示其中一次已解析 mesh draw。Attachment scope、load/store、clear/resolve 和 pass debug identity MUST 保留在具体 pass 的 `RHIRenderPassDesc` 中，不得进入 draw list；`MeshDrawCommand` MUST NOT 保存 `MeshBatch`、Material、Proxy、RenderScene、device、context、queue、frame 或 backend native object 的裸引用。

#### Scenario: 同一 View 具有多个 MeshBatch
- **WHEN** BasePass 为同一 render view 解析出多个合法 MeshBatch
- **THEN** 它 MUST 生成一个对应的 `MeshPassDrawList` 与多个 `MeshDrawCommand`，而不是为每个 batch 创建 pass wrapper 或 attachment descriptor

#### Scenario: Draw 输入失效
- **WHEN** MeshBatch 的 shader、pipeline、vertex/index binding 或必要 logical binding 无法完整解析
- **THEN** 当前 batch MUST 被诊断并排除在 draw list 外，其他合法 batch MAY 继续准备

### Requirement: BasePass 在 render-pass scope 前完成准备
`render_base_pass(...)` SHALL 先按 View 消费 candidate MeshBatches，完成 pass eligibility、shader variant、vertex input、pipeline、uniform upload、logical binding set 和 draw command 物化，再开始对应 RHI render pass。`begin_render_pass()` 与 `end_render_pass()` 之间 MUST 只设置已准备的 dynamic state、pipeline、buffers、完整 logical binding snapshot 并执行 draw；期间 MUST NOT 创建 pipeline、binding layout/set、uniform upload 或回读 Material/Proxy/MeshBatch 准备源。

#### Scenario: 准备成功
- **WHEN** 当前 View 的全部可绘制 batch 已完成资源物化
- **THEN** BasePass MUST 在 `begin_render_pass()` 前得到完整 draw list，再 begin、录制这些 commands 并 end 对应 RHI render pass

#### Scenario: 准备阶段失败
- **WHEN** attachment contract 或 pass 级必要资源在任何 render pass 开始前失败
- **THEN** BasePass MUST 返回原始可诊断错误且不得打开 RHI render pass；外层 frame owner MUST 丢弃当前 recording 并闭合 acquired frame

#### Scenario: 录制阶段失败
- **WHEN** BasePass 已 begin render pass 后任一 command 录制失败
- **THEN** BasePass MUST 尝试结束已打开的 render-pass scope，并向外返回首个业务失败以触发整帧 discard，不能继续 submit 部分 draw

### Requirement: Logical Binding Group 按所有权准备
Global、View、Pass、Material、Object MUST 保持五个 logical Binding Group，不等于 descriptor set。Global/View/Pass 的 canonical CPU values 与 GPU uniform resources SHALL 分别按 renderer、view、具体 pass 的更新频率准备；Material/Object SHALL 按 material instance 与 primitive/draw identity 准备。若一个 command 需要适配不同的 compatible binding layout，MAY 保存重复的轻量 `RHIBindingSetRef`，但同一 View 的 canonical uniform bytes 与 uniform-buffer upload MUST NOT 因 MeshBatch 数量重复生成。

#### Scenario: 同一 View 使用两个 compatible MeshBatch
- **WHEN** 两个 MeshBatch 引用相同 View 参数且其 program 可使用同一 View binding adapter
- **THEN** BasePass MUST 复用同一 View uniform resource和 binding set，不得为第二个 batch 再序列化或上传 View matrices

#### Scenario: 同一 View 使用不同 binding layout
- **WHEN** 两个 program 对 View group 需要不同但合法的 target binding adapter
- **THEN** BasePass MAY为同一 View uniform resource创建各自轻量 binding set，但 MUST共享已上传的 canonical View buffer且不得把差异暴露为 Vulkan physical set

#### Scenario: Vulkan portable mapping
- **WHEN** Vulkan ES3.1 profile 录制包含全部五个 logical groups 的 draw
- **THEN** renderscene MUST仍提交一个完整 `RHIGraphicsBindings` snapshot，由 Vulkan backend 将 Global+View 聚合到 physical set 0、Pass/Material/Object 映射到 set 1/2/3

### Requirement: Mesh pass contract 兼容多 View Shadow 绘制
共享 mesh draw contracts MUST NOT 依赖 camera `ViewInfo` 或 BasePass attachments。未来 ShadowPass SHALL 能为 directional cascade、spot light 或 cube face 分别构造自己的 pass inputs、visibility 结果、pass parameters、attachment scope 和一个或多个 `MeshPassDrawList`；shadow matrix、atlas rect 与 depth bias MUST属于 Shadow pass/light domain，不得伪装成 camera View group。

#### Scenario: Directional light cascades
- **WHEN** 一个 directional light 生成四个 shadow cascades
- **THEN** ShadowPass MUST能够使用四组独立 shadow visibility/parameters 和相应 draw lists，而不修改 BasePass API 或向 `ViewInfo` 写入 cascade/atlas 状态

## Minimal Implementation Example

以下示例是 non-normative；规范性内容以上述 requirements 与 Type Contracts 为准。

```cpp
BasePassInputs inputs{view_infos, scene_color_view, scene_depth_view};

// ForwardSceneRenderer 是 CPU orchestration owner；inputs 只在本次 logical RT 调用中观察数据。
const RHIStatus status = render_base_pass(device, shader_program_cache, context, inputs);
if (!status)
{
    // 外层 frame owner 丢弃 recording，并负责 abort_frame()；BasePass 不 submit/present。
    return status;
}
```
