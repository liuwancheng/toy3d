## Purpose

定义与 Shader Program layout 解耦的五组 logical BindingSet、Pipeline 驱动的 active resource 解析以及各图形后端的 native binding 聚合、验证和 GPU 生命周期行为。

## ADDED Requirements

### Requirement: Logical BindingSet 与 Program layout 解耦
`RHIBindingSet` SHALL 表示恰好一个 Global、View、Pass、Material 或 Object logical group 的不可变资源快照。每个 value MUST 由稳定 ShaderParameterId 和 array index 标识；BindingSet MUST NOT 持有完整 Program binding layout、target slot、physical set、D3D register、root parameter 或 backend descriptor。空或未使用 group MUST 使用 null 引用，不得创建含义不明确的空 BindingSet。

#### Scenario: 同一 View 被不同 target mapping 消费
- **WHEN** 两个 Pipeline 对同一 View constant-buffer ID 使用不同 target slot，但要求相同 resource type 和 constant ABI
- **THEN** 两个 Pipeline MUST 能消费同一个 View logical BindingSet，不得要求 renderscene 创建 layout adapter

#### Scenario: BindingSet value 重复
- **WHEN** 一个 BindingSet 对相同 ShaderParameterId 和 array index 提供重复 value，或提供 invalid ID、空 resource、错误 group ownership
- **THEN** 公共创建入口 MUST 在调用 backend native materialization 前返回可诊断 `InvalidArgument`

### Requirement: 当前 Pipeline 解析 active logical resources
draw/dispatch 前，RHI MUST 以当前 Pipeline active layout 为权威，按 ShaderParameterId、group、resource type、array index 和 uniform data ABI 从五组 logical BindingSet 中解析资源。每个 required active binding MUST 完整存在且兼容；logical BindingSet MAY 是 superset，未被当前 Program 使用的 value MUST NOT 参与 native binding、resource-state validation、transition 或 GPU payload 保活。

#### Scenario: Active binding 缺失
- **WHEN** 当前 Pipeline 要求一个 texture array 的全部元素，而 logical BindingSet 缺少任一 active array index
- **THEN** draw/dispatch MUST 在录制 native draw/dispatch 前失败，且不得发布部分 physical binding

#### Scenario: Superset 包含未使用纹理
- **WHEN** Material BindingSet 包含当前 Program 未使用且未转换到 shader-resource access 的纹理
- **THEN** 当前 draw MUST 只验证和绑定 active resources，不得因该未使用纹理状态而失败

### Requirement: Logical BindingSet 由公共 frontend 创建
logical BindingSet MUST 由公共 RHI frontend 完成 descriptor、resource owner、buffer range、array uniqueness 和 capability-independent结构验证并持有资源强引用；创建 logical BindingSet MUST NOT 分配 Vulkan descriptor set、D3D descriptor table 或其他 backend native binding object。Backend MUST 只在 command recording 中依据当前 Pipeline 物化不可变 native packet。

#### Scenario: 创建跨 device BindingSet
- **WHEN** BindingSet descriptor 包含不属于当前 RHIDevice 的 buffer、view 或 sampler
- **THEN** 公共创建入口 MUST 返回 `InvalidArgument`，不得进入 backend packet 创建

#### Scenario: Native packet 创建失败
- **WHEN** logical set 已合法但 backend descriptor allocation 或 target mapping materialization 失败
- **THEN** 当前 draw MUST 返回原始可诊断错误，不得录制半成品 native bind 或 draw

### Requirement: 五组图形绑定保持原子快照
graphics command recording MUST 继续以一个完整 `RHIGraphicsBindings` 快照提供 Global、View、Pass、Material 和 Object logical sets；字段中的非空 set MUST 与字段 group 匹配且属于当前 device。不同 group 不得再要求来自相同完整 Program layout；required group 与 active value completeness MUST 由当前 Pipeline 判定。

#### Scenario: Vulkan portable 五组绑定
- **WHEN** Vulkan ES3.1 profile 的 graphics Pipeline 同时使用全部五个 logical groups
- **THEN** backend MUST 原子聚合 Global+View 为 physical set 0，并将 Pass、Material、Object 映射为 set 1、2、3，bound descriptor set 数不得超过四

#### Scenario: Pipeline 未使用 Pass group
- **WHEN** RHIGraphicsBindings 的 Pass 字段为空且当前 Pipeline active layout 不包含 Pass binding
- **THEN** binding 和 draw MUST 合法继续，不得要求伪造空 Pass set

### Requirement: Native packet 按兼容物理布局和 active resources 复用
Backend MAY缓存 native binding packet，但 cache identity MUST覆盖对应 physical binding layout signature、全部 active non-dynamic resource identity、uniform backing storage identity、range size和影响 native descriptor 内容的状态。仅 uniform slice offset 改变且 backend 支持等价动态 range binding时，offset MUST作为 bind-time payload 而不得强制创建新 descriptor packet。Debug name、未使用 logical value和完整无关 Pipeline state不得进入 packet identity。

#### Scenario: 两个 Object slice 共享 Vulkan descriptor packet
- **WHEN** 两个 draw 使用相同 Object physical layout、相同 uniform backing buffer和range size，仅 aligned slice offset 不同
- **THEN** Vulkan backend MUST能够复用兼容 descriptor packet并以各自 dynamic offset 绑定正确数据

#### Scenario: Texture view identity 改变
- **WHEN** active Material texture 的 native view identity 改变
- **THEN** backend MUST生成或选择包含新 view 的 packet，旧 packet MUST 保活至其提交 completion

### Requirement: Binding 生命周期覆盖实际 GPU 使用
native packet MUST 强持有本次解析出的 active resources和必要 descriptor allocation，并由完成录制的 command list 保活到对应 queue completion。被丢弃的 command list、录制失败或 submit 失败不得把未提交 packet标记为 in-flight；logical BindingSet 中未被当前 Program 使用的 superset value不要求进入该 command list 的 GPU lifetime root。

#### Scenario: Shader hot reload 后旧 draw 仍在飞行
- **WHEN** 新 Program/layout 已发布而旧 command list 仍引用旧 native packet
- **THEN** 旧 Pipeline、packet和active resources MUST 保持有效直到旧 submission completion，新 logical BindingSet MAY同时被新 Program解析

### Requirement: 三后端和移动 profile 保持同一公共语义
Vulkan、D3D11 FL11_0/SM5 和 D3D12 MUST消费同一 ID-based logical BindingSet contract。Vulkan MAY物化 descriptor sets，D3D11 MUST按 stage/register class 展开并维护 hazard state，D3D12 MAY物化 descriptor table、root descriptor或root constants；这些 native选择不得进入上层。Cook/runtime MUST验证目标 profile的 per-stage、per-set/table、dynamic uniform和总资源 limits，不支持路径 MUST返回 `Unsupported`。

#### Scenario: D3D11 不支持 constant-buffer subrange
- **WHEN** D3D11 FL11_0 backend 无法使用可选的 subrange binding接口
- **THEN** backend MUST使用语义等价的 pooled standalone constant buffer路径或返回明确失败，不得要求上层改用 D3D slot

