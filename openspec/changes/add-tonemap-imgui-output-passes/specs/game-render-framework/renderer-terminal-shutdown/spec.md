## MODIFIED Requirements

### Requirement: Renderer lifecycle 使用固定转换
正常Renderer lifecycle MUST遵守 `Stopped→Starting→Running→Stopping→Stopped`。Starting、Running或Stopping期间发生不可恢复framework/RHI错误时 MUST转入Terminal。同一个Renderer object一旦进入Terminal MUST保持sticky，不能重新发布Running；它只能执行有限teardown并最终销毁。

Starting只有在RHIDevice、RenderResourceManager、placeholder/Tonemap/启用时的ImGui font与buffer-pool bootstrap completion、primary viewport全部成功后才能转为Running。Stopping期间不得重新开放普通frame/resource/UI façade。

#### Scenario: Bootstrap 成功
- **WHEN** Renderer从Stopped开始且全部RT bootstrap步骤成功
- **THEN** lifecycle MUST按Stopped→Starting→Running发布，普通frame/resource/UI façade只能在Running后开放

#### Scenario: Bootstrap terminal failure
- **WHEN** Starting期间device、shader、font或bootstrap submission发生不可恢复错误
- **THEN** lifecycle MUST进入Terminal并执行有限回滚，不得尝试在同一Renderer object上重新Starting

#### Scenario: 正常停止
- **WHEN** Running Renderer收到正常shutdown且没有terminal error
- **THEN** lifecycle MUST按Running→Stopping→Stopped发布，teardown完成前不得提前发布Stopped

### Requirement: RT teardown 顺序
RT teardown MUST依次abort active frame、清 `PrimitiveSceneInfo`/Proxy和RenderScene、terminal/clear RenderResourceManager全部non-owning collections、丢弃尚未执行的UI draw payload与buffer-pool recording状态、释放Renderer-owned representations及Tonemap/ImGui RHI refs、回收已完成queue work与deferred deletion、在device健康时执行有限queue idle/completion收敛、销毁viewport、placeholder和device，最后清façade publication。GT ImGui context只能在Renderer teardown与RenderingThread join完成后由composition root销毁。实际in-flight RHI payload在确认completion前不得销毁。

#### Scenario: 正常 RHI teardown
- **WHEN** device 非 lost
- **THEN** queue wait/回收 MUST 在 native device 销毁前完成，font与动态UI buffer的in-flight引用 MUST遵守对应completion

#### Scenario: Scene 与 Resource 顺序
- **WHEN** final RT teardown开始
- **THEN** RenderScene中的PrimitiveSceneInfo/Proxy MUST先清空，Material/Texture/Mesh representations与Tonemap/ImGui resources随后释放，viewport/placeholder/device最后销毁

#### Scenario: Pending UI Draw 被 terminal skip
- **WHEN** terminal发生时FIFO中仍有携带ImGuiDrawData ownership的Draw command
- **THEN** draw业务体 MUST不执行，payload MUST在logical RT disposal路径析构，GT ImGui context不得等待或回收该payload内部pointer
