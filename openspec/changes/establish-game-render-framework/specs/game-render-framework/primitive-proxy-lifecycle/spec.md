## Purpose

定义 PrimitiveComponent 与 RenderScene 之间 Proxy/SceneInfo 的创建、稳定 identity、FIFO 更新、移除和析构协议，保证跨线程不捕获可变 Game 对象。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `PrimitiveSceneProxy` | 新增 polymorphic class | GT 构造独占 payload，RT-only 可变/读取，最终由 RenderScene/SceneInfo 销毁；不反向访问 Component |
| `PrimitiveSceneInfo` | 新增 class | RenderScene-owned RT 索引节点；拥有 Proxy 并维护场景注册关系 |

不新增 Proxy ID、Handle、Token、revision 或 registry 类型；GT 保存 opaque non-owning `PrimitiveSceneProxy*` 但禁止解引用。

## ADDED Requirements

### Requirement: Add 转移 Proxy ownership
Component 创建 render state 时 SHALL 构造独占 Proxy 并将 ownership 移入 Add RenderCommand；RT MUST 在命令内创建 SceneInfo 并注册到 RenderScene。

#### Scenario: Add 成功
- **WHEN** Add command 执行
- **THEN** RenderScene MUST 拥有 Proxy，Component 只保留 opaque identity

### Requirement: Update 捕获 owned value
Primitive update MUST 捕获稳定 proxy identity 和独立 value payload，不得捕获 World、Actor、Component、Camera 或其他 GT 可变引用。

#### Scenario: Transform 连续更新
- **WHEN** GT 连续两次更新 transform 后投递 Draw
- **THEN** RT MUST 按 FIFO 应用两次 owned value，并在 Draw 中观察最终值

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
