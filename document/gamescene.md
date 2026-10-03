# GameScene：World、Actor 与 Component

## 定位与所有权

代码 engine/runtime/gamescene，属于 Toy3dRuntime；测试 engine/runtime/tests/gamescene_tests.cpp。World 拥有 Actor，Actor 拥有 Component；GT 管可变游戏状态，RT 仅持独立 SceneProxy/owned payload，不直接读 mutable Actor/Component。

World/Actor/Component 的创建、注册、begin/end play、卸载有显式生命周期；添加对象不等于已注册/已开始 play。Editor 可保持编辑状态不运行 gameplay。释放前先撤回渲染注册，不能仅依赖析构顺序让 RT 猜 owner 是否存在。

现有 ActorComponent 顺序是 register_component/on_register → initialize_component/on_initialize → begin_play/on_begin_play，退出 end_play/on_end_play 后 unregister/on_unregister；重复阶段受状态保护。World tick 只允许 begin_play 后，World end_play 按逆 Actor 顺序收尾。派生 hook 不能绕过基类状态机，注册渲染状态和 gameplay play 状态也不能混为同一开关。

## 组件 tick

World 在 GT 同步执行 Actor → Component 两阶段；Actor 只负责自己的 tick，组件业务由 `ActorComponent::tick_component` 派生实现。World 的通用调度不识别 SkeletalMesh 等具体类型。代码仍属于 gamescene / Toy3dRuntime，不新增 target 或独立动画调度系统。

组件默认关闭 tick，通过独立的 `set_tick_enabled` / `is_tick_enabled` 按需启用；启用且注册的组件进入 World 的非 owning 列表，注销先撤回调度。列表生命周期覆盖 Actor/Component 销毁。World 在帧开始复制参与列表，调用前复核组件/owner 的 begin_play、开关和待销毁状态；帧中新增或原先未参与的组件启用后从下一帧执行，关闭或待销毁立即跳过。删除在整个两阶段结束后统一处理，World tick 不允许递归调用。

注册、初始化、begin/end play 和注销 hook 的实际入口使用可嵌套生命周期守卫，hook 内不能进入 World tick；嵌套调用恢复外层守卫。回调请求销毁只标记待销毁，遍历完成后重新定位 Actor 并移除，允许回调追加 Actor 导致容器重分配。直接 spawn/create_component 不在返回引用前销毁新对象，回调中标记的待销毁项由后续 World 收尾处理。

`tick_component` 返回 bool，默认成功；失败由领域组件报告具体原因，World 汇总失败并继续其他组件，不回滚 World 时间或任意业务状态。tick 开关与 Actor 的开关独立，运行状态不自动增加 content revision，也不进入 Scene 持久化格式。当前没有 tick group、prerequisite、间隔调度、物理阶段或并行 tick；以后出现实际需求再扩展调度入口。

SkeletalMeshComponent 默认启用自己的 tick，内部推进动画和发布 pose/bounds；动画失败仍保留原时钟与快照。Editor 编辑 World 不 begin_play、不参与自动 tick，预览继续显式求值。动画领域 contract 见 [Animation](animation.md#world组件与实例状态)。验证见 GameScene/Animation、项目 Actor/PIE 与 Renderer 回归。StaticMeshActor/SkeletalMeshActor 自带各自的 MeshComponent 根组件，支持空资源；骨骼组件的独立 Scene 分支、注入式装配和 Editor 作者行为见 [组件资源绑定](animation.md#scene-组件资源绑定)。

## 组件、附着与更新

- SceneComponent 保存 local TRS 和计算后的 world matrix，父子关系不允许环；跨 Actor 挂接需要同 World、完整生命周期约束。KeepWorld 等需要将矩阵分解为 local Transform 的路径检查 positive-scale TRS 可表示性，失败保留原图与 Transform；不能据此禁止 KeepRelative 产生的合法 world affine matrix。
- 非 root component、独立 Actor 与跨 Actor 父关系须保持正确注册/恢复；不能假定所有 component 都是根、每个 Actor 只有一个场景组件。
- Primitive、Light、Camera 等派生组件保存 validated settings；setter 先验证整个候选，再改 GT、更新 revision、发布 owned RT 数据，不能边改边发现非法字段。
- 可见性、cast_shadows、receives_shadows、enabled、render priority 是不同语义；阴影选择与渲染行为见 [Renderer](renderer.md)。
- MeshComponent 统一具名材质槽、default/override 和 vertex factory 查询，不持有几何或动画；两种具体网格组件承担资源生命周期。
- 每次影响渲染的更新经过 SceneInterface FIFO；Transform/灯光/材质更新保持稳定 Proxy 身份，不能将纯属性修改伪装为 Remove/Add。

## 内容 revision 与持久化

World 的 content revision 用于 Editor 脏状态/外部修改检测，不是帧号或 undo 栈深度。变更内容才增加，读取/纯渲染不增加；Undo 回到已保存内容需要正确身份判断，不能仅靠“同栈深”判干净。

Scene DTO/反射在 core/asset/scene，不能持 runtime 指针。当前 SceneActor schema 6、Scene schema 7 持久化 Component 身份、类型、settings、附着、阴影属性及场景环境。反射注册一个类型不等于自动完成其 runtime 装配和 UI；接入闭环见 [Editor](editor.md)。

## 游戏工程接入边界

项目 `src` 编译为 Runtime 静态模块，例如 ShadowDemo；同一模块用于共享 Editor 动态加载的项目 DLL 和静态链接的独立 Game。Runtime 不依赖项目或 Editor。`GameModuleRegistration` 由宿主显式注入；模块在资产扫描前注册反射 schema，在 World 创建前冻结 ActorTypeRegistry。DLL 生命周期覆盖其对象与注册回调，不支持热重载或自动 C++ 工程生成。构建/工程关联见 [Runtime](runtime.md#工程与分层配置)。

ActorTypeRegistry 分开保存稳定类型名、精确 runtime type、属性 schema、create/validate/capture/apply 和放置模板。create 回调只创建一个属于传入 World 的新 Actor；公共 create 入口检查数量/类型/所有权，错误返回不能接管或删除旧 Actor。回调不得修改旧对象或开始 gameplay。属性是 ReflectedValue 持有的类型/版本/owned bytes；未知类型/版本、无效属性或多余字节拒绝。模块不向引擎 kind switch 添加项目类型。

Scene schema 7 持久化 Actor type/properties；kind 仅保留内置放置类别，自定义类使用 Custom。内置 kind 必须与 Actor type 一致。旧 Scene schema 拒绝，要求按当前格式重建。组件 properties 仍为有限内置 variant，注册新 Actor 不代表支持任意自定义 Component。

`assemble_scene` 复用 Runtime 的组件创建/capture/apply 与 SceneGeometry：先验证类型/属性和解析几何，再构建新 Actor/组件并恢复完整附着图；失败只撤回候选，成功才移除旧场景。资源解析和 Material 赋值通过 SceneAssemblyServices 注入，服务持有者负责对应失败回滚。Editor 保留选择/历史/dirty/保存冲突；Game 完整装配、绑定渲染后 begin_play，关闭先 unregister，再 drain 渲染资源。

实际项目示例见 `project/src/rotating_actor.h/.cpp` 与 `shadow_demo_module.cpp`。RotatingActor 有 enabled、axis 和 speed_degrees_per_second；axis 必须可归一化，speed 必须有限且绝对值不超过 36000。tick 将 delta_seconds 转成角度，绕 root 本地轴组合并归一化 Quaternion，只改 rotation，保留 translation/scale。设置改变递增内容 revision；没有 root 或旋转发布失败时记录日志并停止 tick。编辑 World 不 begin_play，自转发生在独立 Game 或 PIE 的运行 World，运行姿态不写回编辑场景。PIE 生命周期见 [Editor](editor.md#视口内-play)。

## 开发入口与验证

新增/修改 Component 依次检查：稳定类型/字段身份 → settings 整体验证 → setter/revision → owned SceneInterface 更新 → Proxy 行为 → DTO/schema/装配 → Editor capture/apply/Details → Undo/Save/Open。领域策略留模块，通用文件/任务/序列化复用 core。

完整用例以 gamescene_tests.cpp 及 Editor placement/workspace 测试为准，不构造不存在的通用 component 动态反射 API。验证未注册/已注册/playing/退出、父子与跨 Actor 环、失败原子性、更新 FIFO、非 root 恢复、资源删除及 World 切换；renderer ownership 的测试还见 renderer_scene_ownership_tests.cpp。

项目扩展验证见 `project/tests/rotating_actor_tests.cpp`：帧率独立旋转、非法设置、类型/schema、候选失败保留旧场景、参数 Undo/Redo、删除恢复、保存重开及 Saved 快照隔离。

## 场景环境

`SceneEnvironmentSettings` schema 1 保存可选 strong `EnvironmentAssetData` root 引用、可归一化四元数和有限非负强度；无引用表示 Off。`World::set_environment(settings, cube)` 同时检查引用与 CPU payload，归一化旋转并更新内容版本；失败保留旧值。场景装配在替换旧 Actors 前完成环境加载和验证，保存从 World 捕获同一 settings。

`SceneInterface::update_environment` 接收 owned snapshot，经 RenderCommand FIFO 发布。绑定 World 时发送当前环境，解绑时显式发送 Off。CPU Cube 可共享，RenderScene 分域持有独立 TextureResource；资源和旋转/强度在 recording 成功提交后一起发布。取消 recording 保留可重试候选，资源失败释放候选、保留旧 GPU 资源并返回诊断；已有 binding 持有的旧 GPU 引用仍有效。
