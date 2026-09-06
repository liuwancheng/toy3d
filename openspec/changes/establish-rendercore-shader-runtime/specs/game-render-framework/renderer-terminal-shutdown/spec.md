## MODIFIED Requirements

### Requirement: RT teardown 顺序
RT teardown MUST依次abort active frame、清 `PrimitiveSceneInfo`/Proxy和RenderScene、terminal/clear RenderResourceManager全部non-owning collections、释放Renderer-owned representations、Pass resources与直接Program refs、清空并销毁`RHIShaderProgramCache`、释放placeholder refs、回收已完成queue work与deferred deletion、在device健康时执行有限queue idle/completion收敛、销毁viewport和device，最后清 façade publication。实际in-flight RHI payload在确认completion前不得销毁。

冻结 `GlobalShaderMap` 是 GT-owned CPU只读输入，Renderer teardown只释放自身共享引用，不得从logical RT修改或销毁Engine仍拥有的map。无论正常还是terminal路径，任何Shader/Pass/cache cleanup error只能追加secondary diagnostic，不得覆盖first terminal error。

#### Scenario: 正常 RHI teardown
- **WHEN** device 非 lost
- **THEN** Pass-owned Program refs与RHIShaderProgramCache MUST先释放，queue wait/回收 MUST在native device销毁前完成

#### Scenario: Scene 与 Resource 顺序
- **WHEN** final RT teardown开始
- **THEN** RenderScene中的PrimitiveSceneInfo/Proxy MUST先清空，Material/Texture/Mesh representations随后释放，Pass resources、Program cache、placeholder、viewport与device按依赖顺序销毁

#### Scenario: GlobalShaderMap 输入释放
- **WHEN** Renderer final teardown 完成并清除其冻结 map ref
- **THEN** Engine MAY在RenderingThread join后释放自身GlobalShaderMap、ShaderMap与loader，RT不得留下对它们的non-owning引用

### Requirement: DeviceLost 不无限阻塞
DeviceLost或状态未知同步失败后shutdown MUST不无限retry/wait idle。Renderer/backend MUST停止进一步不安全native调用，保留first DeviceLost error，释放Pass-owned CPU wrappers、Program refs与`RHIShaderProgramCache` CPU entries，并走backend允许的有限terminal teardown；无法确认GPU idle时不得把native payload“已安全销毁”作为成功事实，但Engine exit和thread join仍必须有界完成。

#### Scenario: wait idle 返回 DeviceLost
- **WHEN** terminal teardown 无法确认 GPU idle
- **THEN** 系统 MUST记录secondary diagnostic、清除CPU-side Pass/cache ownership并继续有限teardown，不得挂死Engine exit或覆盖first DeviceLost

#### Scenario: Program cache cleanup reports secondary failure
- **WHEN** DeviceLost 已成为 first error 且 Shader/cache cleanup 又产生诊断
- **THEN** RendererStatus primary error MUST仍为DeviceLost，后者只能成为secondary diagnostic

## Type Contracts

本 capability 不新增独占具名类型。它只补充 `RHIShaderProgramCache`、`GlobalShaderMap` 与现有 Renderer/RHI 类型的销毁顺序；这些类型由其唯一所属 capability 登记。

## Minimal Implementation Example

```cpp
// logical RT final teardown；每一步都不得覆盖 first terminal error。
render_scene.reset();
resource_manager->enter_terminal();
release_renderer_representations();
tonemap_pass_resources.reset();
imgui_renderer.reset();
shader_program_cache.reset();
release_placeholders();
finish_or_bound_terminal_queue_cleanup();
primary_viewport.reset();
device.reset();
global_shader_map_input.reset(); // 只释放 Renderer 的共享只读 ref。
```
