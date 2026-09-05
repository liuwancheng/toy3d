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
SceneInterface SHALL只提供以下无返回值fire-and-forget操作，并通过RenderCommand修改RenderScene：

```cpp
virtual void add_primitive(
    std::unique_ptr<PrimitiveSceneProxy> proxy) = 0;
virtual void update_primitive_transform(
    PrimitiveSceneProxy* proxy,
    Matrix4 world_transform,
    AxisAlignedBounds world_bounds,
    bool visible) = 0;
virtual void remove_primitive(PrimitiveSceneProxy* proxy) = 0;
```

这些签名只使用已确认类型和现有Math/Geometry值类型，不新增update payload类型。`PrimitiveSceneProxy*`是GT不得解引用的opaque non-owning identity；`Matrix4`、`AxisAlignedBounds`和`bool`按值进入owned callable。GT MUST NOT通过SceneInterface查询RT可变状态。SceneInterface调用只能在GT同步读取Game-side输入并构造owned payload，投递到RT的callable MUST NOT捕获Component引用。

#### Scenario: GT 查询可见 Primitive
- **WHEN** Game code 尝试通过 SceneInterface 获取 RT visibility 或 RHI resource
- **THEN** 接口 MUST 不提供该能力，调用方应使用独立异步结果协议

### Requirement: bind_scene 为已有 Primitive 补建 render state
已经实现的`World::bind_scene(SceneInterface&)`与`World::unbind_scene()` SHALL保留。World绑定SceneInterface时 MUST为全部已注册PrimitiveComponent补建render state；`bind_scene()`正常返回true时，每个需要render state的Primitive MUST已经完成`add_primitive()`正常返回，因此全部Proxy ownership已被transport接受。重复绑定是唯一正常返回false的bind分支。

普通Add不存在可恢复transport status。Proxy构造或transport contract violation若无法维持强ownership保证 MUST记录诊断并且不得正常返回；World不得实现partial enqueue rollback、第二条fallback queue或no-op Add。normal return后才正式发布可供World后续Component生命周期使用的non-owning SceneInterface。

#### Scenario: 已有 Primitive 的 World 绑定 Scene
- **WHEN** World 已包含两个已注册 StaticMeshComponent 后调用 `bind_scene()`
- **THEN** 两个 Component MUST 分别执行 `create_render_state()`，并通过 `SceneInterface::add_primitive()` 将 Proxy ownership 投递到 RT

#### Scenario: 重复绑定
- **WHEN** World已经绑定一个SceneInterface后再次调用`bind_scene()`
- **THEN** 调用 MUST返回false且保持原绑定和现有render state不变，不得切换到新SceneInterface

#### Scenario: 绑定期间 transport contract violation
- **WHEN** Proxy构造或Add投递无法满足无返回值transport的强ownership contract
- **THEN** 失败 MUST记录诊断并且不得正常返回；World不得把它转换为可恢复false或执行partial enqueue rollback

### Requirement: unbind_scene 先移除 render state
调用方 SHALL 在 `World::unbind_scene()` 前停止为该 World 创建新的 Draw。World MUST 先让全部 PrimitiveComponent 执行 `destroy_render_state()`、投递 `remove_primitive()` 并清空 Component 的 opaque proxy identity，再清空自身 non-owning SceneInterface。正常 Renderer teardown 前，remove commands MUST 通过 RenderCommandFence drain；Renderer terminal 时由 terminal skip/disposal 销毁尚未执行的 ownership payload，World MUST NOT 回读 RenderScene 确认状态。

#### Scenario: 正常解绑
- **WHEN** 一个含有已创建 render state 的 World 调用 `unbind_scene()`
- **THEN** 所有 Primitive remove MUST 先进入 FIFO，Component 与 World 的 non-owning identity 随后清空，Renderer teardown MUST 等待这些 remove commands drain

### Requirement: Scene 生命周期受 Engine/Renderer 编排
World MUST 在 Renderer teardown 前移除全部 render state；RenderScene MUST 在 resource manager 和 RHI device 销毁前清空。

#### Scenario: Engine shutdown
- **WHEN** GT 停止 frame producer
- **THEN** World remove commands MUST 先 drain，RT 才能销毁 RenderScene

#### Scenario: Render-side 销毁顺序
- **WHEN** 正常 shutdown 开始销毁 Renderer domain
- **THEN** World render states MUST 已移除，RenderScene MUST 先清空 PrimitiveSceneInfo/Proxy，随后 RenderResourceManager 才能释放 representations，最后才能 teardown viewport、placeholders 与 device

## Minimal Implementation Example

> Non-normative：本示例只说明推荐的所有权、线程和失败方向；若与 Type Contracts 或 requirements 冲突，以规范性内容为准。

```text
GT:
World contains two registered StaticMeshComponent values
→ bind_scene(scene)
→ each component creates StaticMeshSceneProxy
→ add_primitive() transfers each proxy into a FIFO RenderCommand

RT:
first Add creates PrimitiveSceneInfo
→ second Add creates PrimitiveSceneInfo
→ RenderScene owns both proxies

Failure:
World is already bound
→ a second bind_scene(other_scene) returns false
→ keep the original SceneInterface and render states unchanged.

Transport contract violation:
Proxy construction or Add admission cannot preserve ownership
→ record diagnostics and do not return normally
→ do not expose a recoverable partial-bind result or fallback queue.
```
