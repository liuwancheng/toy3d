# Toy3d Application 与验证项目设计

## 1. 状态、目标与非目标

本文定义项目与 Editor 宿主共用的 `Application` contract。目标是让项目代码通过一个稳定入口接入现有
`Engine` composition root，并把仓库内 `project/` 作为第一份可运行案例，支持后续快速增加引擎模块
验证项目。

第一版不定义 `.toyproject`、Editor New Project、动态 Game Module、热重载、插件、Cook 或独立 SDK；
这些能力出现真实用例后再扩展，不能反向改变本节的 Engine/Application 所有权边界。

## 2. 类型与职责

| 类型 | 所有者 | 职责 |
| --- | --- | --- |
| `Engine` | executable 入口 | 唯一进程级 composition root；拥有 Application、World、Window 和渲染框架，编排生命周期 |
| `Application` | `Engine` | 项目级启动策略和跨帧项目状态；创建初始 World 内容、更新项目级状态、提供当前 View、释放项目级资源 |
| `World` | `Engine` | 拥有 Actor，驱动 Actor/Component 生命周期与 Gameplay Tick |
| `CubeApplication` | `Engine`，经 `Application` 独占所有 | 当前案例的材质、纹理、Camera View 与自动化验收策略 |
| `CubeActor` | `World` | 当前案例的 SceneComponent 层级及逐帧旋转、可见性 Gameplay |

`Application` 不是第二套 Engine，也不是 service locator。它不得暴露 Renderer、RenderScene、RHI、
Task Graph 或其他 Engine 内部可变对象。普通 Gameplay 更新应进入 Actor/Component；只有确实跨越
World 对象或属于项目宿主策略的状态才留在 Application。

文件拖入是可选的 IWindow 平台事件，默认关闭，启用与消费均在窗口 owner thread。事件只携带 owned UTF-8 路径及客户区逻辑坐标，不通过游戏输入映射或触发资源加载；Application 自行决定接收区域和业务确认流程。当前 Windows/macOS Editor 的模型接入、上限和退出清理见 [StaticMesh 导入交互](static-mesh-import-design.md#7-editor-导入与拖放交互)。其他平台启用返回 false。

## 3. 所有权与绑定

```text
executable
└── Engine
    ├── unique_ptr<Application>
    ├── unique_ptr<World>
    │   └── unique_ptr<Actor>
    └── unique_ptr<IWindow>
```

Engine 在 World 和 Window 已创建后绑定 Application。`Application::world()` 与 `window()` 是只对派生类
开放的非拥有 observer；它们只在 `on_initialize()` 开始至 `on_shutdown()` 返回的区间有效。Application
不得删除、替换或在 shutdown 后保存这些对象。

World 和 Window 不在每帧回调之间重复传递。Engine 只在内部绑定入口传入一次，Application 派生类通过
受生命周期约束的 protected accessor 使用它们。

## 4. 生命周期与线程

第一版所有 Application hook 均在 Game Thread 顺序调用：

protected `starts_world_play() const` 默认为 true。Editor 覆盖为 false，主 World 仅初始化并绑定 SceneInterface，保持 Initialized，不执行 Gameplay BeginPlay/Tick；Application 的 UI、宿主 tick 和 View 构建仍正常执行。该启动策略不提供 Play 模式切换；Editor 缩略图另有独立预览 World，不改变主 World 的生命周期。

```text
Engine creates Window and rendering framework
→ Engine creates World
→ bind World/Window observers
→ Application::on_initialize()
→ Application::on_initialize_preview_scene(scene, tasks) # uses_preview_scene() 为 true 时
→ World::initialize()
→ World binds SceneInterface
→ World::begin_play()        # starts_world_play() 为 true 时

each frame:
Window events
→ World::tick(delta)          # 仅 Playing World 驱动 Gameplay
→ Application::on_tick(delta) # project-level policy only
→ Application::on_build_scene_views()
→ submit Draw

shutdown or startup rollback:
Application::on_shutdown()
→ World::end_play()/unbind_scene()
→ destroy World
→ renderer/thread/platform teardown
```

Engine 在调用 `on_initialize()` 前就把 Application 标记为已绑定。即使初始化返回 `false`，也必须调用
一次 `on_shutdown()`，使部分创建的项目资源能够回滚。派生实现的 shutdown 必须幂等；不得假定初始化
已经完整成功。

`on_build_scene_views()` 只构造本帧 owned/copied `SceneView` 输入，不保存 Renderer 或 RenderScene 引用。
未来 GameViewport/Camera 形成正式 contract 后，可以收窄该 hook，但不得让 Application 直接执行 RHI。

### 编辑器图片接入

`uses_preview_scene()` 在 Renderer 启动前确定独立预览 RenderScene 的创建。Renderer 初始化完成后，Engine 将稳定 non-owning `SceneInterface&` 和现有 `TaskGraphInterface&` 注入预览 hook，有效期覆盖 Application shutdown。Application 只拥有预览 World 和业务作业；Renderer 仍拥有 RenderScene、离屏 targets、UI 纹理和 readback。

每帧 Engine 在宿主 tick 前 poll `UiTextureResult`，在 `on_collect_ui_render_work()` 收集 owned 像素上传、预览 view 值和退休 ID。`ui_texture_ids()` 登记可显示图片，ImGui 快照只保存逻辑 ID，RHI 资源全部留在 RT；具体缓存和保存边界见 [Asset 缩略图](asset-thumbnail-design.md)。

## 5. 当前项目组织

```text
project/
├── asset/                # 游戏资产，子目录由使用者组织
├── config/               # 项目配置覆盖
├── cube_test.cpp          # 参数解析、创建 Application、启动 Engine
├── cube_application.*     # 项目资源、View 与验收策略
└── cube_actor.*           # Actor/Component 层级与 Gameplay Tick
```

新增验证案例时应保持同样的职责分离：入口不得重新实现 Engine composition root；Application 不得吸收
本应属于 Actor/Component 的 Gameplay；Actor 不得持有 Window、Engine、Renderer 或后端对象。

后续需要频繁增加案例时，再在不改变上述 contract 的前提下提取 `toy3d_add_project()` CMake helper 和
通用 executable 入口。第一版先使用 Cube 案例验证 Application 生命周期本身，不引入模板生成。
Editor 与 Cube 已共用 `engine/build/cmake/deploy_resources.cmake` 的开发资源复制规则；引擎/项目资产、
配置和平台图标边界见 [资源目录设计](resource-directory-design.md)。

## 6. 验证矩阵

- 无 Application 时使用 Engine 的空 Playing World 与默认 View；Editor Application 使用 Initialized World；
- Cube Application 初始化成功后，Actor Tick、Material 更新和 SceneView 构造保持原行为；
- Application 初始化失败时执行一次 shutdown，并按 Engine 原有路径回滚；
- single-thread 与 multi-thread 使用相同 Application 调用顺序；
- 正常窗口关闭和 Cube 自动关闭路径都在 World/Renderer teardown 前释放项目资源；
- `project/` 不直接获取 RenderScene、RHI 或具体 backend。
