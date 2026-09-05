# game-render-framework/primitive-proxy-lifecycle Specification

## Purpose
定义 PrimitiveComponent 与 RenderScene 之间 Proxy/SceneInfo 的创建、稳定 identity、FIFO 更新、移除和析构协议，保证跨线程不捕获可变 Game 对象。

## Requirements

### Requirement: Add 以 fire-and-forget 转移 Proxy ownership
PrimitiveComponent 在注册到已绑定 SceneInterface，或 World 绑定 SceneInterface 并补建现有 Primitive render state 时，SHALL 执行 `create_render_state()`。StaticMeshComponent 的该流程 MUST 从当前 GT 值构造独占 `StaticMeshSceneProxy`，暂存其 opaque non-owning pointer，并由无返回值 `add_primitive()` 将 ownership 移入 Add RenderCommand；RT MUST 在命令内创建 `PrimitiveSceneInfo` 并注册到 RenderScene。

`add_primitive()`、`update_primitive_transform()` 与 `remove_primitive()` 都是建立在无返回值 `enqueue_render_command()` 上的 fire-and-forget 领域入口，不传播 transport status。只有 `add_primitive()` 正常返回后，Component 才能发布 opaque proxy identity；正常返回 MUST 保证 Proxy ownership 已经同步消费或被 Task Graph transport 接受。GT 始终禁止解引用该 opaque pointer。

#### Scenario: Add 成功
- **WHEN** Add command 执行
- **THEN** RenderScene MUST 拥有 Proxy，Component 只保留 opaque identity

#### Scenario: Add 正常返回
- **WHEN** `add_primitive()` 正常返回到 `create_render_state()`
- **THEN** Proxy ownership MUST 已由 logical RT 同步消费或由 Task Graph transport 接受，Component MAY 发布其 opaque non-owning identity

#### Scenario: Add transport contract violation
- **WHEN** `add_primitive()` 在 façade 未建立、producer错误或Task Graph无法接受已声明可接受任务的情况下调用transport
- **THEN** transport MUST记录诊断并fail fast，不得正常返回或要求Component恢复已经移入callable的ownership

#### Scenario: Add 与 terminal 竞争
- **WHEN** Renderer刚进入terminal而GT有一条已经开始的Add投递
- **THEN** transport MAY在producer-stop前接受Proxy ownership，logical RT MUST跳过Add业务体并析构Proxy；Component中的opaque identity不得解引用，GT观察terminal后 MUST清空render state

### Requirement: Component 使用显式 render-state 生命周期
已绑定 SceneInterface 的 PrimitiveComponent MUST 通过 `create_render_state()` 建立 Proxy，通过 `send_render_transform()` 发送 transform/bounds 更新，并通过 `destroy_render_state()` 发起移除。World transform 变化 MUST 先完成 Component bounds 更新，再发送 render transform；注销或 World unbind MUST 先 destroy render state，再断开对应 SceneInterface 关系。

terminal竞争窗口内的`destroy_render_state()` MAY投递一个只进入FIFO skip/disposal路径的remove command，随后 MUST清空Component opaque identity；该命令不得在业务执行许可已关闭后访问RenderScene或解引用可能已经由terminal disposal销毁的Proxy。

#### Scenario: StaticMeshComponent world transform 变化
- **WHEN** 已创建 render state 的 StaticMeshComponent 更新 world transform
- **THEN** Component MUST 更新 world bounds，并由 `send_render_transform()` 调用 `update_primitive_transform()` 投递 owned value payload

### Requirement: Update 捕获 owned value
`update_primitive_transform()` MUST接收opaque `PrimitiveSceneProxy*`、按值复制的`Matrix4 world_transform`、`AxisAlignedBounds world_bounds`和`bool visible`。RenderCommand捕获这些独立值，不得捕获World、Actor、Component、Camera或其他GT可变引用，也不新增具名update payload类型。

第一阶段StaticMesh或material-slot reference发生结构变化时 MUST通过`destroy_render_state()`后`create_render_state()`重建Proxy；不得把mesh/material更新偷塞进名称只表达transform的操作，也不得新增尚未确认的resource-update接口。MaterialInstance内部参数变化仍通过稳定`MaterialRenderProxy`的专用material update路径处理，不重建Primitive render state。

#### Scenario: Transform 连续更新
- **WHEN** GT 连续两次更新 transform 后投递 Draw
- **THEN** RT MUST 按 FIFO 应用两次 owned value，并在 Draw 中观察最终值

#### Scenario: StaticMesh 引用变化
- **WHEN** 已创建render state的StaticMeshComponent切换到另一个StaticMesh
- **THEN** Component MUST先`destroy_render_state()`投递remove并清空旧opaque identity，再从新Mesh值执行`create_render_state()`；不得调用`update_primitive_transform()`替换mesh reference

#### Scenario: MaterialInstance 参数变化
- **WHEN** Component引用的同一MaterialInstance只修改scalar/vector/texture参数
- **THEN** 稳定MaterialRenderProxy MUST通过material update FIFO更新，PrimitiveSceneProxy无需因参数值变化重建

### Requirement: 可见 StaticMeshSceneProxy 贡献 frame-local MeshBatch
`compute_view_visibility()` MUST 只让通过当前 `ViewInfo` 视锥测试且仍注册在 RenderScene 的 `StaticMeshSceneProxy` 参与 mesh 收集。Proxy SHALL 使用其 StaticMeshRenderData、material slots 和 `LocalVertexFactory` 为当前帧贡献 `MeshBatch`；`MeshBatch` MUST NOT 成为 GT 持久状态或跨帧 ownership 容器。本 requirement 不引入尚未确认名称的收集接口。

#### Scenario: Proxy 通过视锥测试
- **WHEN** `StaticMeshSceneProxy` 的 world bounds 对当前 View 可见且其 `StaticMeshRenderData` 通过整体可绘制 gate
- **THEN** Proxy MUST 为其可绘制 sections 贡献当前帧 `MeshBatch`

#### Scenario: Proxy 已移除
- **WHEN** remove command 已从 RenderScene 摘除对应 `PrimitiveSceneInfo`
- **THEN** 后续 visibility 与 `MeshBatch` 收集 MUST NOT 再访问该 Proxy

### Requirement: Remove 先摘除再析构
Remove command MUST 先从 RenderScene 的所有索引和 draw 数据中摘除 SceneInfo，再在 RT 销毁 Proxy；重复或失序 remove MUST 产生可诊断 contract violation。

#### Scenario: Component 销毁
- **WHEN** Component 销毁 render state
- **THEN** GT MUST 清空 opaque pointer，RT MUST 在后续 Draw 前完成摘除

### Requirement: Proxy 与资源释放排序
所有引用 Mesh、Material 或 Texture representation 的 Proxy remove/update MUST 排在对应 representation release command 之前。

#### Scenario: 最后一个 Mesh 使用者移除
- **WHEN** 最后一个 Primitive 停止使用 Mesh
- **THEN** remove/update MUST 先于 Mesh render data ownership-transfer release
