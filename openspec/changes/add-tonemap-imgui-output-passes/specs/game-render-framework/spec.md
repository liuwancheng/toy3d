## MODIFIED Requirements

### Requirement: 一帧端到端顺序
正常帧 MUST 按 Window/Input/ImGui event processing、Game updates、UI frame构建、`SceneView`/`SceneViewFamily` 与一次性UI draw payload输入、RT FIFO Proxy/Resource 更新、`init_views()`、`compute_view_visibility()`、`MeshBatch` 收集、`LocalVertexFactory` 与 `ShaderVertexInput` 匹配、Forward Base Pass、HDR SceneColor Tonemap pass、可选独立ImGui pass、业务 submit、presentation 与独立 GPU completion 的顺序执行，且 CPU Fence、submit commit 和 GPU completion MUST 保持不同语义。Runtime的Tonemap与ImGui MUST使用独立RHI render pass，并MUST录制到同一graphics command list、由一次business submit提交；Editor离屏Scene Viewport路径保持相同pass职责分离。

#### Scenario: 正常 frame N
- **WHEN** Game Thread 完成 frame N 的 Window/Input处理、World tick、UI构建、状态更新和 Draw 投递
- **THEN** logical Rendering Thread MUST 在 Draw 前应用 FIFO 更新，在同一business list内依次完成Base Pass、Tonemap和可选ImGui，成功 submit 后发布资源状态，并由独立 completion 控制 GPU payload 回收

#### Scenario: Editor 离屏 Scene Viewport
- **WHEN** 未来Editor把Scene View渲染到可被UI采样的独立LDR texture
- **THEN** Tonemap MUST先完成该texture输出与所需Store/sampled transition，随后Editor ImGui在自己的独立最终输出pass中组合它
