# Editor 开发规范

本文是 Toy3d Editor 框架、场景创作与新功能接入的唯一规范入口，描述当前实现。涉及材质、导入、资源文件、渲染和线程的专项 contract 仍以 `document/index.md` 中的 Active 文档为准。新增面板、组件属性或资产编辑器前，先阅读本文及相关公共接口。

## 1. 框架与依赖

Editor 使用一个原生主窗口承载 ImGui Dockspace，场景输出是离屏图像。它复用 Engine、World、RenderCore、Reflection/Serialization 和 AssetPairStore，没有独立的运行时对象系统、GC 或通用插件装载器。

```text
Toy3dEditor（组合入口）
├── EditorWorkspace：创作文件系统、冻结 schema、资产索引、文件对发布
└── Engine：World、Window、Renderer；调用 EditorApplication 的 Application 回调
    └── EditorApplication
        ├── EditorSceneSession：活动场景文件、稳定身份、装配、保存、场景历史
        ├── ActorFactory：Actor archetype、内置几何、mesh 创作来源
        │   └── ComponentEditorRegistry：组件 capture/apply/create/Details 回调
        ├── EditorSelection、SceneViewport、MaterialAssignments
        ├── EditorPanelRegistry：绘制顺序、Window 菜单、活动编辑命令路由
        ├── AssetEditorRegistry：按稳定根类型分派打开请求
        └── 各面板与资产工作流：材质编辑、纹理预览、导入、缩略图、shader 编译
```

| 位置 / target | 职责与依赖 |
| --- | --- |
| `engine/core/scene_data/` / `Toy3dSceneData` | Primitive、Light、LocalLight、DirectionalShadow、Camera 设置值、默认值、比较和校验；只依赖 Math |
| `engine/core/scene_asset/` / `Toy3dSceneAsset` | Actor/Component 文件 DTO、生成 codec、身份/引用/图验证；依赖 Resource 与 SceneData，不依赖 World |
| `engine/runtime/gamescene/` / `Toy3dRuntime` | World/Actor/Component 生命周期、设置权威值、领域 setter、向 RenderCore 发送更新 |
| `engine/editor/source/scene/` | 场景会话、组件快照、运行时 ID 与持久 ID 映射、候选场景装配 |
| `engine/editor/source/components/` | 组件注册与手写 Details，普通函数回调；不在中央面板堆积类型分支 |
| `engine/editor/source/commands/` | 场景历史、手势、保存点、删除/重建后的身份重映射 |
| `engine/editor/source/panels/` | 面板、轻量注册、对话框；`scene_panels.cpp` 只协调选择与组件绘制 |
| `engine/editor/source/asset_tools/` | 资产打开注册和领域创建/导入/发布策略 |
| `engine/editor/source/workspace/` | 创作 mounts、TypeRegistry、Catalog、AssetPairStore；不拥有活动资产编辑会话 |
| `engine/editor/source/material/` | MaterialEditSession、MaterialAssignments、源码工作流；shader 异步流程属于 `Toy3dEditorMaterialShader` |
| `engine/editor/source/placement/`、`viewport/`、`selection/` | 放置、观察相机/拾取/Gizmo、集中选择 |
| `Toy3dEditorScene` | 上述场景操作、注册、视口与 Details 的可复用静态库；Editor 与集成测试链接同一实现 |

依赖方向是 UI → Editor 领域工作流 → Core/GameScene/RenderCore 公共接口。Runtime、工具和 Core 不反向包含 Editor/ImGui；上层不访问后端类型、RHI texture 或 SceneProxy。公共基础设施先查 [Core 使用索引](core-module-usage-index.md)，不在面板另造文件、线程、日志、事务或 ID 系统。

## 2. 所有权、线程与退出

- Engine 独占 World、Window、Renderer。Application 和 SceneSession 借用 World，不拥有第二份活动场景。Component 的当前值是权威；保存时才提取完整快照。
- World 独占 Actor，Actor 独占 Component。EditorSelection 和长寿命命令保存值身份，取用时重新查找存活对象；不缓存跨帧 Actor/Component 裸指针。
- Workspace 在 Application 之前建立，退出后才释放。FileSystem、冻结 TypeRegistry、Catalog 和 AssetPairStore 的寿命覆盖所有领域会话。
- Application 持有 SceneSession、注册表、面板和工作流。ComponentEditorRegistry 由 ActorFactory 持有，启动注册后冻结。注册表只存行为描述；面板回调借用的服务必须由 Application 保证寿命。
- MaterialEditorPanel 独占 MaterialEditSession、资产撤销历史和预览候选；`edit_session()` 是已初始化编辑器的借用入口。Workspace 不持有这类用户编辑状态。其他资产类型也由自己的编辑器持有领域会话。
- GT 串行执行 UI、World/Asset 修改和索引发布。后台 shader 编译、纹理加载与导入持有 owned 输入/候选，通过 GT 验证后接管；不从后台捕获 UI 控件或写 World。
- 退出顺序：清除注册回调、停止后台工作、取消交互、关闭资产预览/缩略图、注销场景对象、排空 RenderCommand，最后释放材质、几何和工作区。GPU 未结束使用前不能释放相关资源。

`EditorApplication` 负责组合、菜单、布局、关闭确认和输入协调。可复用场景行为进入 SceneSession/History；领域操作进入相应工作流，不把解析器、每种组件属性处理或新会话塞进 Application。

## 3. Runtime 属性与 Reflection 的联系

### 3.1 设置值与 setter

Runtime Component 持有共享领域设置，例如 `DirectionalLightComponent::shadow_settings()`。Core SceneAsset 保存同一种 `DirectionalShadowSettings`；Editor 操作候选值，通过已注册 `apply` 回调调用 Component aggregate setter，成功后再发出渲染更新。RenderProxy 持有独立镜像，不借用 Editor 草稿。

新增字段按此顺序接入：

1. 在职责明确的领域设置中声明字段、单位、默认值、反射标记；补齐 `operator==` 和 `is_valid`。
2. Runtime 增加访问接口；整组候选先校验、再赋值。同值不触发更新，失败不改变此前字段。
3. setter 在实际作者值变化时调用 `World::mark_content_changed()`；需要渲染更新时走现有 SceneInterface。
4. 在该组件编辑器中补充控件。默认 capture/apply 已传递整个设置值，不再维护一套 Editor 扁平属性镜像。
5. 核对文件 schema、已登记 codegen 输入、已有资产和测试；类型/字段改名或变型不能悄悄复用旧文件版本。

`World::content_revision()` 表示对象、层级与属性内容变化，即使尚未绑定 SceneInterface 也有效。`scene_generation()` 继续用于渲染/拾取失效；渲染缓存重建本身不会让场景文件变脏。

### 3.2 反射基础 contract

`TOY3D_REFLECT_TYPE`、`TOY3D_REFLECT_ENUM`、`TOY3D_PROPERTY` 在 C++ 编译时展开为空；稳定类型/属性名称来自标记参数。反射仅描述显式声明的创作值，不负责构造 World、生成 UI 或按内存偏移直接写 Component。

生成器只读取 CMake 明确登记的头文件，不递归展开 include。结构/字段标记与声明各占一行，公开字段位于 `struct`；枚举为有明确整数值的 `enum class`。持久化名称采用 ASCII 标识符及点。支持固定宽度数值、bool、string、Core Math、登记的嵌套结构/枚举、vector、有限 variant、AssetRef；拒绝私有/保护字段、指针和未知类型。生成文件只进入构建目录，新增含设置的头文件必须同时登记到 codegen 输入和依赖。

`TypeRegistry` 显式注册、检查冲突后冻结，后续只读；不同 executable 只注册自身可消费的领域模块。`PropertyDesc` 描述稳定名字、值类型、用途和显示提示，不以 C++ 偏移作为文件 ABI。

- `Edit` 可编辑，`Visible` 只读，`Transient` 不保存；未标记字段不进入 schema，非 Transient 标记字段默认保存。
- Category、Range、Unit、AssetType 是提示，真实合法性由领域校验决定。
- `Toy3dSerialization` 有界校验字节、字符串、数组、嵌套深度和 UTF-8，返回带偏移/路径的 ValueStatus；确定性输出不依赖 unordered 容器顺序。
- `PropertyPath` 使用稳定字段名、数组下标、稳定元素 ID 选择器和 tagged variant 分支；可重排集合不得用数组下标当身份。
- `EditSession<T>` 组合候选、领域校验、预览准备与完整 old/new 历史；用途、类型、引用及预览均成功才发布。修改线程必须是 owner 线程。它不持有 RHI/World，不用于伪装场景命令。
- Material 连续手势使用草稿，结束向已有 EditSession 提交一次复合修改；参数权威仍是 `.shader Properties`，按 ShaderParameterId/名称绑定，不能用 native slot 反推作者身份。

### 3.3 Editor 宏

共享设置不是 Editor 专用数据，Runtime 必须能包含它们；`Edit` 不意味着 Editor-only。当前 GameScene 类型没有因 Editor 宏形成不同布局。

`WITH_EDITOR` 隔离确需编辑器的行为，`WITH_EDITORONLY_DATA` 隔离确需随类型存在的创作数据；新增条件字段不能让链接到同一程序的目标看到不同布局/schema。Editor target 的 PRIVATE 定义不会重新编译 Runtime target。不要仿照其他引擎在 Runtime 头中引入 UI、选中状态或未提交草稿；这些由 Editor 会话持有。

## 4. 接入新组件 Details

入口是 `components/component_editor_registry.h` 的 `ComponentEditor`，包含 exact runtime type、稳定文件 type、显示名，以及 `capture/apply/create/draw_details` 四个普通函数回调。注册拒绝缺项和重复身份；冻结后拒绝变更。Exact RTTI 仅用于进程内查找，不进入文件。

1. 定义/复用领域设置和 Runtime setter。需要保存的新组件，为 `SceneComponentData::properties` 增加明确 typed 分支，在 `validate_component_data()` 中登记 type/分支配对和完整不变量，并更新 codegen 输入及版本策略。当前 schema 是显式支持类型的闭合集合，不是任意运行时类的自动持久化。
2. 新建 `components/<domain>_component_editor.cpp`。capture 读整个设置；apply 经领域 setter；create 调用 `actor.create_component<T>()`，返回 Actor 拥有的 SceneComponent。
3. 在 ComponentEditorRegistry 的启动注册中加入一个描述项。新增 Actor archetype 时另扩 ActorFactory/PlacementCatalog 的构建、描述和 Scene kind 校验；不能仅登记 UI 后宣称对象可还原。
4. `draw_details(ComponentDetailsContext&)` 使用手写领域控件。先 `capture_component_edit(context, candidate)`，控件只改 candidate，随后 `finish_component_edit(context, candidate, changed)`。默认 Transform 已由中央面板绘制，不重复绘制。
5. Application 每帧只调用一次 `EditorPanelRegistry::process_shortcuts()` 处理 Ctrl+Z/Y/S，面板不得再次消费同一快捷键，否则会产生重复撤销或误保存场景。点击菜单项/选择列表属于离散操作，需要明确 begin → preview_component → finish；不能依赖 ImGui Combo 弹出内容的 activation 标志捕获起始状态。连续 drag 则由控件 activation/deactivation 合并一条历史，Escape 恢复起始值并结束控件活动状态。视口只能在自身图像区域的有效点击中结束 Details 手势，不能消费全局左键点击。
6. 登记源文件到 `Toy3dEditorScene`，测试非法复合值不部分修改、非 root 编辑、Undo/Redo、保存/打开、对象重建及 attachment。

`scene_panels.cpp` 只枚举所有 owned SceneComponent 并分派绘制；不添加针对新类型的 dynamic_cast 分支。组件回调不得存跨帧指针、绕过 setter、读 RenderProxy 或直接发布资源文件。设置改变了但不影响画面，也必须记录 content revision。

StaticMesh 的几何来源由 ActorFactory 跟踪：内置 Cube/Plane 或 mesh AssetRef。额外 mesh 必须 `remember_mesh()` 登记来源，且来源与当前实际几何一致；材质覆盖通过 MaterialAssignments 按 Component ID + 稳定 slot name 管理。没有创作来源的临时运行时 mesh/override 拒绝保存。

## 5. 接入新面板与资产编辑器

### 5.1 普通面板

面板有自己的文件和跨帧状态，由 Application 持有；绘制使用 ImGui Begin/End。搜索过滤器、待确认删除和错误文本属于面板实例，禁止函数静态可变状态；shutdown 清除注册回调后重置这些状态。通过 `EditorApplication::register_panels()` 添加 `EditorPanel`：

```cpp
EditorPanel panel;
panel.id = "animation_editor";             // 稳定且唯一
panel.title = "Animation Editor";          // Window 菜单文字
panel.window_name = "Animation Editor";    // 与 ImGui Begin 的完整名字一致
panel.draw = [this]() { animation_editor_.draw(); };
// 有自己的编辑历史时，同时提供 undo、redo、focused 三个回调；保存快捷键由 save 回调接入。
// 检查 panels_.add(std::move(panel)) 的返回值；所有注册完成后统一 freeze。
```

这是接入模式，Animation Editor 尚未实现。只读面板无需历史回调。场景编辑面板复用 SceneSession 的 history；资产编辑器使用自身会话历史，不能另建公共 Undo singleton。

注册顺序就是绘制顺序，Window 菜单由同一描述项生成。注册拒绝重复 id/window_name、缺失 draw 或不完整历史/焦点回调；初始化后冻结。新增面板默认可浮动/停靠；若需要默认停靠位置，修改 Application 的默认 DockBuilder 布局，同时保持已有持久窗口名。`Scene Viewport###Game Viewport` 的后缀是保留既有布局身份，不随标题重命名。

Window 菜单用于聚焦已绘制窗口；需要文件才能显示的资产面板由打开动作建立会话。不可假定聚焦操作能创建缺失的编辑对象。最后聚焦的可编辑面板决定 Edit 菜单和 Ctrl+Z/Y/S 路由；聚焦菜单/只读面板保留之前的编辑目标。shutdown 清除注册回调后才能释放依赖。

### 5.2 资产编辑器

Content Browser 返回通用 `asset_open` 与 `asset_focus`；Application 按 Catalog 的根类型调用 AssetEditorRegistry。新编辑器登记 `{root_type, request_open}`，而不是添加 browser.material_open 等专用事件和 Application 类型分支。

领域编辑器负责自己的会话、候选预览及 Save/Discard/Cancel；request_open 只是提出打开请求。一次打开失败保留旧会话和预览。TexturePreviewPanel 的请求、CPU 候选和 GPU 上传与当前显示状态分开；上传确认成功后才切换 AssetId、属性和图像并退役旧纹理。失败或迟到结果只丢弃候选。新资产类型还需在 Workspace 注册领域 schema、添加资产导入/创建与索引规则；渲染预览另走现有逻辑 UI texture/渲染输出路径。只登记打开回调不会自动提供导入、预览、菜单或保存能力。

资产保存复用 AssetPairStore；保持 AssetId、SubresourceId 与资源 slot/source key，不能用路径、数组下标或渲染对象地址当身份。重导入必须保持既有身份，完整候选准备成功再发布；无法稳定匹配时报告 orphan，不猜测新顺序。

### 5.3 输入与 UI 特殊规则

- 输入优先级：模态框/文本输入 → 活动 Gizmo/控件 → 视口交互/HitProxy → 编辑快捷键 → 游戏输入。导入、资产切换或保存确认期间不触发场景删除和历史命令。
- 拖动发生在场景图像区域；面板只通过标题/tab 移动。viewport 的 extent 使用实际物理像素，Editor 相机独立于场景 Camera。
- HitProxy 结果验证请求 ID、viewport/scene generation 和对象存活；过期结果不能覆盖新选择。资源 drag delivery 返回 owned 放置请求，之后按 AssetId 重新解析再执行命令。
- Outliner 双击按 bounds 聚焦；方向光方向箭头不依赖选中态，保持短小的屏幕长度。视口支持滚轮 zoom、右键 orbit、中键 pan。
- 集中选择保留 Actor 和 Asset 两类 ID，最后主动选择决定 Details 焦点。浏览 Asset 不抹掉 Actor；材质拖入 Details 时仍展示保留 Actor 的槽位。
- 目前 Gizmo 操作 Actor root；Details 可修改所有已注册组件的位置/scale，旋转通过 root Gizmo。View Camera 当前以 CameraActor root 为目标，非 root Camera 只编辑参数；扩展组件级选择/相机查看需要明确新增身份 contract。
- 创建和导入位于 File 或 Content Browser 右键菜单，路径默认 /Project。Texture2D 单击预览、双击聚焦；Rescan 刷新已发布资产及缓存，不等于重新导入源文件。

## 6. 场景文件、候选与保存点

当前 Scene schema 为 **5**，`root_type=toy3d.SceneAssetData`，纯 UTF-8 `.scene` 描述，不使用 meta。格式版本、引用、文件对事务及可选段规则见 [Asset 格式](asset-pair-format-design.md)。同 stem 的 `.asset` 和 `.scene` 可并存，Scene 配对路径预留 `.scene.meta`；目前拒绝 Scene meta。旧扁平 Scene schema 不在生产入口自动迁移，源码 ShadowDemo 已改为当前结构。

每个 Actor 记录稳定 id、kind、root_component_id 和 components。每个 Component 记录稳定 id、type、parent_component_id、局部 Transform 与 tagged properties。Actor/Component 身份均为非零 128 位、小写 32 位 hex，在整个场景内唯一；它们不同于 World 局部整数 ID。Parent 可以指向同 Actor 或其他 Actor 的任何已保存 SceneComponent，图必须两端存在且无环。

支持的 Actor archetype 是 EmptyActor、Cube、Plane、StaticMesh、DirectionalLight、PointLight、Camera；可组合已登记 Scene、StaticMesh、DirectionalLight、PointLight、Camera 组件。内置 Actor 构造所需组件必须在记录中，不能留下多余默认组件；EmptyActor 按记录精确构造。未知 Actor/组件、非法值/图、缺失资源或无法还原的覆盖明确失败，不静默丢弃。未支持的 ActorComponent 也不能保存为一个不完整 Actor。

SceneSession 的 replace/open：先验证文件、schema、引用、身份与图，准备全部几何，再装配候选 Actor/组件/材质/attachment。候选全部成功后才清除旧交互并替换旧场景；失败销毁候选、保留原 World/选择/历史。GT 中临时构造引起的 content revision 在完整回滚后由 history 明确确认，不能把一次失败打开显示成用户修改。不得将失败后的部分候选加入正常身份映射。

SceneSession 的 save：要求活动手势已结束，只写 `/Project/*.scene`。捕获当前 World 的全组件设置与稳定引用，验证并编码，通过 AssetPairStore 发布。打开时从同一次读取取得已校验的场景内容与原始文件字节作为基线，不能再读取第二份文件建立基线。覆盖保存前比较已发布原始文件字节，外部修改报告 Conflict；发布成功后才推进基线和保存点。Catalog 刷新失败明确告知“已保存但刷新失败”，不能删除成功保存的文件。资产操作使用冻结 mount，禁止写 `bin/` 部署副本。

SceneHistory 的语义：

- 连续 Details/Gizmo 操作按 Actor 记录完整组件 before/after，一次手势一条历史；恢复/重做经过同一注册 apply 与领域校验。
- 空撤销/重做栈是无操作；执行失败保持历史栈不变并返回原因，由 Application 记录并显示 Scene Error，成功重试清除旧错误。
- 取消恢复起始值，不增加历史；拖回同值不创建记录。保存前不得提交仍在交互中的中间值。
- revision 每次提交使用新身份，分支后清 redo；saved revision 支持 Undo 回到保存点、Redo 回到保存点，不能因栈深相同误判干净。
- World 内容发生历史外修改时保守标脏，保存后确认；这类修改没有自动生成 Undo 记录。新增 Editor 写入口必须走命令边界，不能依靠外部标脏替代历史。
- 创建/删除重建 fresh Actor/Component/runtime resources，成功后重映射前后历史、材质槽、子 Actor attachment 与 SceneSession 稳定身份。重建失败不能推进历史或发布身份映射。
- dirty 查询通过 revision/content revision，O(1)，不每帧编码整个场景。文件内容、游戏运行状态、HitProxy、撤销历史、观察相机、面板布局和选中状态之间有明确边界；后六项不写入 Scene。

## 7. 文件、单位、错误与平台

创作 mounts 是 `/Project` → `project/asset`（可写）、`/Engine` → `engine/asset`（只读）、`/Editor/Resources` → Editor 界面资源（只读）、`/Saved` → 部署 saved（缓存）。Runtime 使用自己的只读部署 mounts；两者共用 Core FileSystem 实现，不共享创作写权限。目录职责见 [资源目录](resource-directory-design.md)。

长度固定 **1 unit = 1 cm**，位置、裁剪面、光照范围和阴影距离均保存 cm。默认量级用 `meters_to_centimeters()` 等具名单位接口表达；UI 像素、scale、rotation、bias、fade 是无量纲/独立单位，不能套长度换算。模型导入 scale 在界面/导入设置给出，不在底层硬编码 x100；源节点变换由模型生产链处理，不隐式居中或缩放模型。

各层返回 ReflectionStatus/ValueStatus/AssetStatus 或领域 bool 与原因，调用方检查后再消费结果。底层不弹 UI，不重复记录错误，不引入全项目 Result/诊断框架。未知类型/必需段失败，可选内容不能无损保留时拒绝保存。读取检查容量、深度、UTF-8、有限数值、版本和引用；VirtualPath 校验后访问，不持久化主机路径或对象指针。强引用图环必须失败，弱/延迟关系由 schema 明示。

Windows/macOS 共用领域代码和持久化名称，原生文件选择只存在 platform 实现；不支持的平台返回可诊断失败。模型导入仍同步执行，大模型可能阻塞 UI；纹理导入通过 Task Graph 在后台准备 owned 数据、在 GT 发布，取消后的结果不得发布；自动重导入、任意运行时类型持久化、多文档并发编辑、组件级 Gizmo 选择尚未实现，不得在新面板文案中当作已有能力。

## 8. AI 施工与验收

1. 先读 `document/index.md` 和本文，定位现有入口与相关 Active 专项设计。不要重新读取/复活旧 Editor 方案，不默认搜索历史归档。
2. 判断新增的是面板、组件、资产领域工作流还是共享基础设施。共享能力先盘点与设计；面板不重复实现文件/任务/日志/序列化等公共系统。
3. 先说明 owner、寿命、GT/后台发布点、失败保留范围和保存/历史行为，再改职责最接近的文件。只为真实领域/生命周期概念添加类型，不预建空“framework/manager”目录。
4. 每次新增可编辑持久值，都串起设置 → setter → content revision → 注册 capture/apply → UI/历史 → schema/保存 → 测试。新面板分别串起持有状态 → 注册 → 绘制/焦点 → 历史/关闭 → shutdown。
5. 删除被替代的正式入口与重复数据镜像；不长期新旧双轨，不为旧 Scene 格式重新补兼容分支。不要删除仍有独立职责的渲染、资源或游戏场景 contract。
6. 遵守仓库 C++17/注释/命名/RAII 与 CMake 规范。修改后由独立 sub-agent 配置、构建并测试；主 agent 根据诊断修复并最终复查。
7. 完成后同步本规范及索引，规范只保留当前 contract 和限制；施工流水账、逐轮构建输出和阶段性计划不进入此文。

| 验证 | 必须关注 |
| --- | --- |
| 设置/setter | 无效复合候选不部分修改；同值无更新；无 RenderProxy 时内容变更仍可检测 |
| 组件/场景 | 非 root 属性、精确组件数量、内部及跨 Actor attachment、稳定身份、缺失/未知类型、候选失败保留原场景 |
| 历史/保存 | 连续手势合并、取消、Undo/Redo、保存点分支、删除重建、外部内容标脏、磁盘冲突 |
| 注册/UI | 重复/晚注册拒绝、顺序稳定、焦点历史路由、关闭后不调用失效依赖、当前布局兼容 |
| 反射/资源 | 生成清单、冻结注册、限制/未知字段、确定性 codec、同 stem 文件、AssetPairStore 发布与恢复 |
| 回归 | 原有材质手势/预览/编译、槽位覆盖、多线程发布、缩略图、视口、Runtime 生命周期与 scene ownership |

主要可执行证据在 `engine/editor/tests/editor_framework_tests.cpp`、`placement_tests.cpp`、`workspace_tests.cpp` 及现有 Material/Thumbnail 测试；Core reflection_codegen/resource/serialization 测试覆盖通用基础。Windows 常用配置/构建/CTest 命令以 AGENTS.md 为准；其他平台未经执行不能宣称已验证。
