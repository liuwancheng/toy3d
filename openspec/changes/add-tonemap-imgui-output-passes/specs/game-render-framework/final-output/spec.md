## Purpose

定义 Forward Renderer 从 HDR SceneColor 到最终输出目标的 Tonemap 行为、资源状态、颜色编码、Runtime独立Tonemap/ImGui pass顺序与未来Editor离屏viewport兼容边界，并保证该闭环可由Vulkan、D3D11、D3D12和VulkanPortable v1一致实现。

## Type Contracts

| 类型 | 性质与职责 | 所有权与线程 | 错误语义与复用理由 |
| --- | --- | --- | --- |
| `TonemapParameters` | 新增 value type；保存当前输出所需的手动曝光和 SDR output encoding 参数，不保存 View、Texture、RHI 或 backend 状态 | GT 可按帧构造并随 SceneRenderer ownership 传递；logical RT 只读 | 非有限参数在录制前返回可诊断失败；不能复用 Material 参数，因为 Tonemap 是 Renderer-owned Global/Pass shader，不具有 Material identity |
| `TonemapPassResources` | 新增 RT-only owner；持有已验证的 Tonemap ShaderMap Program 对应 RHI shader、binding layout、sampler 与可复用 pipeline 引用 | Renderer domain 在 logical RT bootstrap 创建、teardown 释放；command list 保活录制引用到 queue completion | 任一必需资源失败使启用 Tonemap 的 Renderer bootstrap 失败；不能复用 MaterialRenderProxy，因为它不拥有材质可变参数或资源策略 |

## ADDED Requirements

### Requirement: Renderer Pass 常量与资源声明分离
Tonemap 与 ImGui shader 的数值常量 MUST通过 `.shader` 的 `Parameters { Pass { ... } }` 声明，并由 compiler 聚合为一个受 ToyShaderABI 约束的 Pass constant buffer；texture、sampler 与 buffer MUST继续通过 `Resources` 声明。第一阶段自定义Parameters MUST只允许Pass group、`Float`/vector与`Float4x4`，MUST不暴露native `cbuffer`、register、descriptor set、数组、struct或多个自定义constant buffer。

#### Scenario: Tonemap 与 ImGui 参数布局
- **WHEN** Tonemap声明`exposure_ev : Float = 0.0`且ImGui声明`projection : Float4x4`
- **THEN** 每个Program MUST得到独立、稳定、可reflection验证的Pass constant buffer，且VulkanPortable仍只使用Pass对应的physical set 1

#### Scenario: 参数放入Resources
- **WHEN** shader作者尝试在`Resources`中把`Float`或`Float4x4`作为resource type声明
- **THEN** frontend MUST返回可定位语法诊断，不得把数值常量隐式解释为descriptor resource

### Requirement: SceneColor 是独立 HDR 场景附件
Forward Base Pass 的 `SceneColor` MUST 使用 `R16G16B16A16Float`，并请求完整 `RenderTarget | ShaderResource` usage；它 MUST 与 presentation texture 的 format、所有权和生命周期分离。Base Pass shader MUST保留可超出 0..1 的有限 HDR radiance，不得在 Tonemap 前统一 clamp 到 SDR。

#### Scenario: SDR backbuffer 上的 HDR 场景
- **WHEN** viewport backbuffer 为 `B8G8R8A8UNorm`
- **THEN** Base Pass MUST仍写入独立的 `R16G16B16A16Float` SceneColor，不能因最终输出为 8-bit 而降低 SceneColor format

#### Scenario: HDR format usage 不受支持
- **WHEN** 当前 device/profile 不支持 `R16G16B16A16Float` 的完整 `RenderTarget | ShaderResource` 组合
- **THEN** Renderer MUST返回可诊断 `Unsupported`，不得创建低精度替代资源或绕过 Tonemap

### Requirement: Tonemap 提供确定的 SDR 输出
第一阶段 Tonemap MUST 对线性 HDR SceneColor 应用有限、确定的手动曝光和 filmic tone curve，并为 `B8G8R8A8UNorm` 最终目标输出显式 sRGB display encoding；alpha MUST输出确定值。非有限输入或参数 MUST被规范化或拒绝，不能把 NaN/Inf 传播为未定义 framebuffer 内容。

#### Scenario: 高亮输入
- **WHEN** SceneColor 包含大于 1.0 的有限线性颜色
- **THEN** Tonemap MUST保留相对亮度关系并把结果压缩到合法 SDR 输出范围，而不是在采样前直接截断

#### Scenario: 默认曝光
- **WHEN** 调用方没有覆盖曝光参数
- **THEN** Tonemap MUST使用已定义的中性手动曝光默认值，并在 Vulkan、D3D11 与 D3D12 上产生等价 output encoding

### Requirement: Tonemap 输出不绑定 swapchain
Tonemap MUST 接收调用方提供的合法 color attachment 及 extent/view rect，不得假定目标一定来自 `RHIFrameContext`。Runtime MAY把目标设为当前 backbuffer；未来 Editor MUST能把目标设为独立 LDR Scene Viewport texture，随后再由 UI 组合。

#### Scenario: Runtime 直接输出
- **WHEN** Runtime 主 viewport 的最终目标是当前 presentation texture
- **THEN** Tonemap MUST直接写该 backbuffer，不得先创建等尺寸中间 LDR texture再 copy

#### Scenario: Editor 离屏 viewport
- **WHEN** Editor 为可停靠 Scene Viewport 提供独立 LDR color attachment
- **THEN** 相同 Tonemap 语义 MUST可输出该 attachment，且不得要求持有 swapchain、present token 或 ImGui 状态

### Requirement: Runtime 使用独立 Tonemap 与 ImGui Pass
Tonemap 与 ImGui SHALL录制为两个独立RHI render pass，同时保持Tonemap先于ImGui的固定逻辑顺序。Tonemap覆盖主backbuffer时MUST使用 `Discard/Store` 完成并结束自己的pass；存在有效UI draw data时，ImGui随后MUST以 `Load/Store` 打开同一backbuffer并alpha blend。两个pass MUST位于同一个graphics command list并由一次business submit提交，且MUST不共享同一个RHI render-pass attachment scope。

#### Scenario: Runtime 存在 UI draws
- **WHEN** Tonemap 覆盖主backbuffer且当前frame包含有效ImGui draw data
- **THEN** frame MUST先结束执行 `Discard/Store` 的Tonemap pass，再以 `Load/Store` 开启ImGui pass并保留Tonemap结果

#### Scenario: 没有 UI draws
- **WHEN** 当前 frame 的 ImGui payload 为空
- **THEN** Tonemap pass MUST仍完成输出和Store，UI阶段MUST不产生空draw或空ImGui pass

#### Scenario: Editor 分离路径
- **WHEN** Tonemap 输出 Scene Viewport texture 而 ImGui 输出 Editor backbuffer
- **THEN** 两个pass MUST使用各自合法attachment scope，Scene Viewport texture的Store与后续sampled transition不得被省略

### Requirement: 最终输出状态与 submit truth 分离
正常 Runtime frame MUST在 Tonemap 前把 SceneColor 从 `RenderTarget` 转为 `ShaderResourceGraphics`，把 backbuffer 从 `Present` 转为 `RenderTarget`，并在全部最终输出 draws 后转回 `Present`。这些 final access 只有 business submit 成功后才能发布；recording failure、frame abort 或明确 submit failure MUST丢弃 local final state。

#### Scenario: ImGui 录制失败
- **WHEN** Tonemap 已录制但随后 ImGui RHI draw 录制失败
- **THEN** 整个 business command list MUST丢弃并通过 `abort_frame()` 闭合 acquired frame，Tonemap 结果和任何 local final access均不得发布

#### Scenario: Submit 成功而 present OutOfDate
- **WHEN** 最终输出 business list 成功 submit 但 present 返回 `OutOfDate`
- **THEN** SceneColor sampled access、backbuffer present access和资源 publication MUST保留，只安排后续 presentation rebuild

### Requirement: 最终输出保持跨后端公共语义
Tonemap MUST只使用公共 texture/view、Pass binding、graphics pipeline、viewport/scissor、render pass、draw 和 transition 语义。Vulkan backend SHALL映射 attachment/layout/barrier，D3D12 SHALL映射 RTV/SRV 与 resource state，D3D11 SHALL映射 RTV/SRV 并解除冲突绑定；VulkanPortable v1 路径不得依赖 Vulkan 1.2+、bindless、subpass/input attachment 或额外 descriptor set。

#### Scenario: D3D11 FL11_0
- **WHEN** D3D11 backend 实现该 pass
- **THEN** 它 MUST能以 SM5 fullscreen draw、SRV/sampler 和 RTV 完成相同 SDR 输出，不得要求 compute、UAV 或显式 native render pass
