# GameScene：World、Actor 与 Component

## 定位与所有权

代码 engine/runtime/gamescene，属于 Toy3dRuntime；测试 engine/runtime/tests/gamescene_tests.cpp。World 拥有 Actor，Actor 拥有 Component；GT 管可变游戏状态，RT 仅持独立 SceneProxy/owned payload，不直接读 mutable Actor/Component。

World/Actor/Component 的创建、注册、begin/end play、卸载有显式生命周期；添加对象不等于已注册/已开始 play。Editor 可保持编辑状态不运行 gameplay。释放前先撤回渲染注册，不能仅依赖析构顺序让 RT 猜 owner 是否存在。

现有 ActorComponent 顺序是 register_component/on_register → initialize_component/on_initialize → begin_play/on_begin_play，退出 end_play/on_end_play 后 unregister/on_unregister；重复阶段受状态保护。World tick 只允许 begin_play 后，World end_play 按逆 Actor 顺序收尾。派生 hook 不能绕过基类状态机，注册渲染状态和 gameplay play 状态也不能混为同一开关。

## 组件、附着与更新

- SceneComponent 保存 local TRS 和计算后的 world matrix，父子关系不允许环；跨 Actor 挂接需要同 World、完整生命周期约束。KeepWorld 等需要将矩阵分解为 local Transform 的路径检查 positive-scale TRS 可表示性，失败保留原图与 Transform；不能据此禁止 KeepRelative 产生的合法 world affine matrix。
- 非 root component、独立 Actor 与跨 Actor 父关系须保持正确注册/恢复；不能假定所有 component 都是根、每个 Actor 只有一个场景组件。
- Primitive、Light、Camera 等派生组件保存 validated settings；setter 先验证整个候选，再改 GT、更新 revision、发布 owned RT 数据，不能边改边发现非法字段。
- 可见性、cast_shadows、receives_shadows、enabled、render priority 是不同语义；阴影选择与渲染行为见 [Renderer](renderer.md)。
- 每次影响渲染的更新经过 SceneInterface FIFO；Transform/灯光/材质更新保持稳定 Proxy 身份，不能将纯属性修改伪装为 Remove/Add。

## 内容 revision 与持久化

World 的 content revision 用于 Editor 脏状态/外部修改检测，不是帧号或 undo 栈深度。变更内容才增加，读取/纯渲染不增加；Undo 回到已保存内容需要正确身份判断，不能仅靠“同栈深”判干净。

Scene DTO/反射在 core/asset/scene、core/asset/scene，不能持 runtime 指针。当前 SceneActor/Scene schema 5 持久化 Component 身份、类型、settings、附着及阴影属性。反射注册一个类型不等于自动完成其 runtime 装配和 UI；接入闭环见 [Editor](editor.md)。

## 游戏工程接入边界

工程框架见 [Runtime](runtime.md#工程与分层配置)。当前仅资源工程；Scene kind 白名单和固定 properties variant 不支持任意工程自定义类型。TypeRegistry 元数据不自动提供 runtime factory/编解码/UI。C++ host 与共享场景装配尚未实现，不能只注册类型就声称工程可加载。

后续接入须先确认稳定类型/schema、owned 属性编码、依赖枚举、构造/验证/capture/apply 与显式格式迁移。现有 EditorSceneSession 装配仍在 Editor；Undo/dirty/选择/保存冲突属于 Editor，运行时创建/资源解析/属性应用/附着验证拟归 GameScene。类型注册需早于扫描/freeze，未知必需类型拒绝候选；Game 在完整场景就绪后 begin_play，失败不发布半场景。

## 开发入口与验证

新增/修改 Component 依次检查：稳定类型/字段身份 → settings 整体验证 → setter/revision → owned SceneInterface 更新 → Proxy 行为 → DTO/schema/装配 → Editor capture/apply/Details → Undo/Save/Open。领域策略留模块，通用文件/任务/序列化复用 core。

完整用例以 gamescene_tests.cpp 及 Editor placement/workspace 测试为准，不构造不存在的通用 component 动态反射 API。验证未注册/已注册/playing/退出、父子与跨 Actor 环、失败原子性、更新 FIFO、非 root 恢复、资源删除及 World 切换；renderer ownership 的测试还见 renderer_scene_ownership_tests.cpp。
