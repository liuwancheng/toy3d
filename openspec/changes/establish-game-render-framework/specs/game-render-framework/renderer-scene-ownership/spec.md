## Purpose

定义 Renderer、World、SceneInterface 与 RenderScene 的 UE 风格所有权和线程边界，使游戏世界不嵌入渲染场景，也不跨线程读取渲染可变状态。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `Renderer` | 新增 class | Engine-owned stable shell；logical RT 初始化并独占 RenderScene、RHI domain 和 terminal state；GT 仅观察 façade 结果 |
| `SceneInterface` | 新增 abstract class | RenderCore 定义的 GT 调用边界；World non-owning 持有；不暴露 RT 查询或 RHI 类型 |
| `RenderScene` | 新增 class | Renderer-owned、RT-only 可变场景；拥有 SceneInfo/Proxy，不依赖 Game 对象 |

## ADDED Requirements

### Requirement: Renderer 拥有 RenderScene
Renderer SHALL 在 logical RT 创建、拥有并销毁 RenderScene；World MUST NOT 创建、嵌入或拥有 RenderScene。

#### Scenario: World 绑定 Renderer
- **WHEN** World 创建 render state
- **THEN** World MUST 获得稳定 non-owning SceneInterface，RenderScene ownership MUST 留在 Renderer

### Requirement: SceneInterface 是单向命令边界
SceneInterface SHALL 只提供 create/update/destroy render state 所需操作，并通过 RenderCommand 修改 RenderScene；GT MUST NOT 通过它查询 RT 可变状态。

#### Scenario: GT 查询可见 Primitive
- **WHEN** Game code 尝试通过 SceneInterface 获取 RT visibility 或 RHI resource
- **THEN** 接口 MUST 不提供该能力，调用方应使用独立异步结果协议

### Requirement: Scene 生命周期受 Engine/Renderer 编排
World MUST 在 Renderer teardown 前移除全部 render state；RenderScene MUST 在 resource manager 和 RHI device 销毁前清空。

#### Scenario: Engine shutdown
- **WHEN** GT 停止 frame producer
- **THEN** World remove commands MUST 先 drain，RT 才能销毁 RenderScene
