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

Scene DTO/反射在 core/asset/scene，不能持 runtime 指针。当前 SceneActor/Scene schema 6 持久化 Component 身份、类型、settings、附着及阴影属性。反射注册一个类型不等于自动完成其 runtime 装配和 UI；接入闭环见 [Editor](editor.md)。

## 游戏工程接入边界

项目 `src` 编译为 Runtime 静态模块，例如 ShadowDemo；同一模块分别链接项目 Editor/Game 宿主。Runtime 不依赖项目或 Editor。`GameModuleRegistration` 由宿主显式注入；模块在资产扫描前注册反射 schema，在 World 创建前冻结 ActorTypeRegistry。没有 DLL 加载、热重载或自动 C++ 工程生成。构建/工程关联见 [Runtime](runtime.md#工程与分层配置)。

ActorTypeRegistry 分开保存稳定类型名、精确 runtime type、属性 schema、create/validate/capture/apply 和放置模板。create 回调只创建一个属于传入 World 的新 Actor；公共 create 入口检查数量/类型/所有权，错误返回不能接管或删除旧 Actor。回调不得修改旧对象或开始 gameplay。属性是 ReflectedValue 持有的类型/版本/owned bytes；未知类型/版本、无效属性或多余字节拒绝。模块不向引擎 kind switch 添加项目类型。

Scene schema 6 持久化 Actor type/properties；kind 仅保留内置放置类别，自定义类使用 Custom。内置 kind 必须与 Actor type 一致。schema 5 YAML 只走显式候选迁移，按旧 kind 映射类型并填空 ActorSettings，保留 Asset/Actor/Component ID、组件、材质和附着；读取不改源文件，下次正常保存写 schema 6。组件 properties 仍为有限内置 variant，注册新 Actor 不代表支持任意自定义 Component。

`assemble_scene` 复用 Runtime 的组件创建/capture/apply 与 SceneGeometry：先验证类型/属性和解析几何，再构建新 Actor/组件并恢复完整附着图；失败只撤回候选，成功才移除旧场景。资源解析和 Material 赋值通过 SceneAssemblyServices 注入，服务持有者负责对应失败回滚。Editor 保留选择/历史/dirty/保存冲突；Game 完整装配、绑定渲染后 begin_play，关闭先 unregister，再 drain 渲染资源。

实际项目示例见 `project/src/rotating_actor.h/.cpp` 与 `shadow_demo_module.cpp`。RotatingActor 有 enabled、axis 和 speed_degrees_per_second；axis 必须可归一化，speed 必须有限且绝对值不超过 36000。tick 将 delta_seconds 转成角度，绕 root 本地轴组合并归一化 Quaternion，只改 rotation，保留 translation/scale。设置改变递增内容 revision；没有 root 或旋转发布失败时记录日志并停止 tick。Editor World 不 begin_play，因此只在独立 Game 中自转。

## 开发入口与验证

新增/修改 Component 依次检查：稳定类型/字段身份 → settings 整体验证 → setter/revision → owned SceneInterface 更新 → Proxy 行为 → DTO/schema/装配 → Editor capture/apply/Details → Undo/Save/Open。领域策略留模块，通用文件/任务/序列化复用 core。

完整用例以 gamescene_tests.cpp 及 Editor placement/workspace 测试为准，不构造不存在的通用 component 动态反射 API。验证未注册/已注册/playing/退出、父子与跨 Actor 环、失败原子性、更新 FIFO、非 root 恢复、资源删除及 World 切换；renderer ownership 的测试还见 renderer_scene_ownership_tests.cpp。

项目扩展验证见 `project/tests/rotating_actor_tests.cpp`：帧率独立旋转、非法设置、类型/schema、候选失败保留旧场景、参数 Undo/Redo、删除恢复、保存重开及 Saved 快照隔离。
