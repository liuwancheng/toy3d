## Purpose

定义 Global、View、Pass 和 Object 高频常量在一次 recording 中的 transient uniform 数据上传、切片、对齐、所有权和完成值回收语义，避免按 draw 创建独立 GPU buffer。

## ADDED Requirements

### Requirement: Recording context 提供 transient uniform allocation
正在录制的 RHI command context SHALL 接收 canonical ToyShaderABI bytes与对应 data layout identity，并返回可放入 logical BindingSet 的 uniform buffer slice。调用返回前 MUST复制全部源 bytes到 context/backend拥有的 storage；返回值 MUST表达 bindable buffer identity、aligned offset、logical data size和必要 layout identity，不得暴露 mapped pointer、native allocation或descriptor handle。

#### Scenario: 同一帧分配多个 Object constants
- **WHEN** 一个 recording 为多个 Object 顺序上传合法 constant bytes
- **THEN** backend MUST能够从一个或少量分页 storage 返回互不重叠的 aligned slices，而不是要求每次创建独立长期 GPU buffer

#### Scenario: 源内存立即失效
- **WHEN** transient upload 调用成功后调用方立即释放或复用源 byte vector
- **THEN** 后续 command recording和GPU execution MUST仍使用调用返回前复制的完整数据

### Requirement: Transient slice 遵守跨后端范围语义
每个 slice MUST满足当前 device uniform-buffer offset alignment、最大 uniform range和backend额外要求；logical data size与native allocation padding MUST分离，padding MUST清零且不得改变 ToyShaderABI。Vulkan/D3D12 MUST使用completion-scoped分页 storage；D3D11 FL11_0在无可靠 subrange接口时 MUST允许内部退化为 pooled standalone constant buffer而不改变公共返回语义。

#### Scenario: D3D12 需要 256-byte 对齐
- **WHEN** logical constant buffer 只有 80 bytes而D3D12 native CBV要求256-byte对齐
- **THEN** slice MUST保留80-byte logical ABI identity，并使用满足native对齐且padding清零的allocation range

#### Scenario: 超过 uniform limit
- **WHEN**请求的 logical data size 超过 device/profile 支持的最大 uniform range
- **THEN** allocation MUST返回可诊断 `Unsupported`，不得截断、分页成多个未声明binding或返回空成功

### Requirement: Transient allocation 生命周期由 recording 和 completion 闭合
transient uniform slice MUST只用于创建它的 device和允许的 recording生命周期；完成的 command list MUST保活其实际引用的allocation page或fallback buffer直到queue completion。discard、recording failure和submit failure MUST按未提交路径释放或回收，成功提交前不得把page发布为可复用。

#### Scenario: 多 frame in flight
- **WHEN** frame N+1 开始分配时frame N使用的uniform page尚未达到queue completion
- **THEN** frame N+1 MUST使用其他可用空间或新page，不得覆盖frame N仍在GPU读取的bytes

#### Scenario: Command list 被丢弃
- **WHEN** recording在提交前失败并丢弃command list
- **THEN** 对应transient allocations MUST不得进入in-flight集合，并可按明确的未提交回收规则安全复用

### Requirement: 上层在业务 render-pass scope 前准备 uniform 数据
RenderCore/RenderScene MUST在具体业务 pass 的 `begin_render_pass()` 前完成所需 canonical bytes生成、transient allocation和logical BindingSet创建。Render-pass scope内只允许选择已准备的Pipeline、binding snapshot、dynamic state和draw/dispatch；不得回读Material、View或Primitive源数据临时生成constant bytes。

#### Scenario: Object uniform allocation 失败
- **WHEN** BasePass准备某个MeshBatch的Object transient slice失败
- **THEN** 该batch MUST在render pass开始前被诊断并排除；pass-level必要资源失败时整个pass MUST在未begin状态返回原始错误

### Requirement: Persistent 与 transient constant 数据职责分离
Global、View、Pass和高频Object constants MAY使用transient allocation；跨帧稳定且仅在参数变化时更新的Material constants SHALL由Material资源策略选择persistent buffer/candidate replacement。Logical BindingSet MUST使用统一buffer slice语义消费二者，不得让allocation策略改变Shader identity或backend mapping。

#### Scenario: Material 参数未变化
- **WHEN** Material constants、texture view identity、sampler和schema generation跨帧保持不变
- **THEN** MaterialRenderProxy MUST能够继续复用persistent logical binding，不得因新frame强制分配transient Material constants
