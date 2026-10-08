# Editor：框架、接入与编辑一致性

## 定位与 ownership

engine/editor/source，程序 Toy3dEditor；可复用库 Toy3dEditorCore。功能目录为 scene（history/selection/placement/components）、viewport、assets（material/texture/mesh/thumbnails）、panels、workspace、platform；资产专属窗口放对应 assets 子目录，通用面板注册留 panels。EditorApplication 是 GT composition root，持 Workspace、selection/history、registries、异步工作流并明确退出；runtime/core 不依赖 ImGui/Editor。

Engine 拥有编辑 World，EditorSceneSession 管描述身份、读取/装配/保存状态；Editor 不启动这个 World 的 gameplay。EditorPlaySession 拥有独立运行 World，缩略图 preview World 也独立。文件创作写 project/asset，Engine 资产只读，Saved 仅缓存，不写 bin。

## 接入：面板、组件与资产

- PanelRegistry 统一 draw/menu/focus/shortcut；每个面板/全局动作每帧恰好一次，不分散处理 Ctrl+Z/Y/S。窗口状态归实例，不用 static 跨会话共享。
- ComponentEditorRegistry 用显式 create/capture/apply/Details；runtime RTTI 只用于对应关系，不成为持久化身份。新增组件不在中央累计 dynamic_cast 分支。
- 新字段闭环：settings 整体验证 → setter/content revision → DTO/反射/schema/codegen → registry capture/apply/Details → 手势/history → Scene 装配/Save/Open → 测试。UI metadata 不代替完整候选校验。
- AssetEditorRegistry 按 root_type 注册，不只按扩展名；窗口保 own session/dirty/history，候选读取失败保持旧 preview/session，不能先清空再加载。
- Texture preview 等资源须 GPU-ready 才替换显示；Editor 不保 Vk* 或私建 device/view ownership。

Tools 是 Scene、Create、Import、Shaders、日志目录与退出的统一菜单入口；资产右键和材质窗口复用已有工作流，不重复实现命令。Ctrl+S/Z/Y 与未保存确认保持原有路由。Recompile Shaders 编译全部登记源的已保存源码；批次禁重复启动，可取消。单窗口和批量共用 source/shader/ShaderWorkflow 的独占队列，不绑定某个材质窗口、不保存草稿/history。用途、发布分组、缓存恢复见 [Shader](shader.md)，材质事务见 [Material](material.md)。

消息提示由 EditorApplication 持有、在 GT 绘制于主窗口右下角，不参与 docking、不自动抢焦点。Shader 任务使用结构化身份/阶段/发布计数/诊断；同一任务原位更新，不能解析状态文字推测成功。成功短时显示、悬停暂停，失败不自动计时消失；取消等待实际结束，关闭提示不取消任务。普通 Error/Critical 从同一 LogBuffer 按 sequence 聚合，启动旧日志不重放；编译日志关联任务提示避免重复，文件日志故障持续可见。最多三张卡片，优先淘汰成功提示，详细输出和超出上限的旧错误仍在 Console/文件；定位被过滤或清空的日志时显式展示独立目标详情，不改过滤设置。退出不保留捕获已销毁窗口的回调。该展示机制限 Editor，不新增共享任务或日志系统及 target。

Console 作为 PanelRegistry 中的普通面板，由 EditorApplication 持有。读取启动入口注入的 Core LogBuffer，生产线程不调用 ImGui；关闭窗口继续采集，打开后可查看启动日志。Trace/Debug/Info/Warning/Error/Critical 独立勾选，默认 Info 及以上，提供全选/仅错误/恢复默认，文本搜索与等级过滤同时生效。清空仅移动显示起点，不清文件；等级计数、截断/淘汰和文件写入故障必须可见。支持复制正文、自动滚动和打开日志目录，不抢编辑焦点、不成为 Undo/Save 目标。Console 面板只查看日志，命令/变量能力仍属 Runtime config。

失败在最终处理操作的入口上报一次，低层保留 status 返回，消息附 Actor/Component/slot/Asset 等上下文。已上报的材质赋值、RHI、Vulkan validation 不重复记录；外部编译器诊断由 Process 捕获后转入 Logger，不能依靠重定向全进程 stdout/stderr 收集。

## 工程与 Scene 菜单

工程与配置格式见 [Runtime](runtime.md#工程与分层配置)。顶层 Scene 提供 New/Open Project、Project Settings、New/Open/Save/Save As Scene；Tools 保留 Material/Shader 创建、资源 Import、Shaders、日志目录和退出。复用 session/history/未保存确认，无第二套命令系统。未指定工程直接打开正式引擎默认 Scene，有工程按 Editor.StartupScene 加载；Shader 验证期间暂缓场景编辑，Console/消息卡片继续工作。

加载期间仍创建 DockSpace 并绘制禁用交互的 Scene Viewport，供真实场景附件初始化及 Material Shader GPU 验证使用；不能等验证完成才创建视口，也不能用空操作验证替代附件就绪检查。

无工程可浏览引擎资产和编辑临时场景，创作 Material/Shader/Import 要求项目写入根。引擎默认 Scene 加载后 clean；修改后 Save 走项目 Save As，禁止写引擎资产。无工程 Save 引导创建资源工程，并将当前 Scene 保存为该工程的 Startup.scene，再关联新工程。

EditorProject::create 先在父目录的独占 staging 建目录/配置，最后写 .toy，再以 no-replace 重命名发布；已存在目标不覆盖。普通失败只清理本次独占 staging，不承诺断电时的目录发布持久性。新工程默认继承引擎场景，资源目录为空。项目和资源工程统一使用 Toy3dEditor，入口按描述动态加载一个 Runtime 模块；不生成 C++ 模板或自动构建。

首版一个进程关联一个工程，切换启动目标 Editor 实例；继承显式命令行配置覆盖。后台编译/Import 完成或取消后才切换，材质/场景 dirty 依次确认；launch 接受后关闭当前窗口，launch 失败保持会话。启动进程成功不等于完成 GPU/资源初始化，不作跨进程 ready 保证。Project Settings 保存 Editor/Game 默认场景覆盖并显示当前有效值来源，下次启动生效。

验证入口 project_tests.cpp、editor_framework_tests.cpp、material_shader_tests.cpp；UI 模态期间屏蔽修改快捷键。界面、默认场景装配、未保存切换仍需真实交互验证，CPU DTO 测试不能替代画面验证。

Scene > Standalone Play 仅在关联已构建 Game 的项目 Editor 中可用，等待启动验证/编译/Import/属性手势完成。捕获当前场景到工程 Saved/play/<新 ID>.scene，再独立启动 Game；无需保存编辑中的场景，不改变作者文件、场景身份、dirty 或撤销历史，运行状态不回写。启动通知仅说明进程接受启动，失败详情看工程 Saved/logs 的 game 日志；无跨进程 ready/停止按钮，关闭 Game 窗口结束运行。快照是可删除缓存，关闭 Game 后可清理 Saved/play。

### 视口内 Play

Scene Viewport 右上角提供 Play/Pause/Resume/Stop 图标按钮，悬停显示动作名，保留原 docking 窗口身份。无工程/资源工程可运行内置 Actor；项目 Actor 使用当前 Editor 已加载、已注册的模块，不依赖 Game 可执行文件。首版单个会话，不含 Simulate/Eject、多客户端、运行状态回写或热重载。

- source/scene/EditorPlaySession 属 Toy3dEditorCore，由 EditorApplication 持有。启动捕获当前未保存内容到内存 SceneAssetData，复用 assemble_scene 和冻结注册表，不落盘。运行 Actor/Component、Mesh 渲染数据、MaterialLibrary 与可变材质实例独立；编辑对象、选择、history、dirty 和观察相机保持原样。不能仅以两个 World 中相同的 Actor ID 认定对象相同。
- Renderer 提供独立 Play RenderScene，Engine/Application 帧提交选择对应 SceneInterface，复用主视口附件和 RHI。编辑渲染注册始终保留；移除最后一个 Mesh Proxy 会终结 Mesh 资源生命周期，不能反复解绑/重绑编辑 World，也不能跨 RenderScene 共用这种 MeshRenderData。缩略图/材质预览与动画交互预览使用各自独立的 Preview Scene。Game/PIE 共用 gamescene/scene_view.h 的相机规则，停止恢复编辑观察相机。
- GT 帧边界执行状态转换。Starting 等待逻辑 RT 检查资源并成功提交场景帧后才 begin_play，等待时间不计入游戏时间；30 秒没有就绪帧则撤回候选并诊断。SceneRenderFeedback 为每次启动独立的共享反馈，release/acquire 发布结果，Ready 不代表 GPU 已完成。Playing 只 tick 运行 World，Paused 不推进 tick/时间但继续绘制。
- 停止先释放输入、end_play/解绑/销毁运行 World，再 drain RT 撤回命令、关闭 MaterialLibrary、释放 geometry；GPU 生命周期仍遵守既有资源引用保留规则。启动与停止清理拾取请求，旧反馈不能接管新会话。点击运行图像获取输入，Shift+F1、焦点丢失、文本/modal 或暂停释放，Esc 停止；ImGui 帧尾后设置有效捕获策略，事件和键盘 Hold 不绕过捕获。运行期间新建的 InputBindingContext 在停止后移除，项目代码不得覆盖宿主已有 context。
- 运行期间 Scene/资产编辑、Undo/Redo、导入、Shader 发布和工程切换禁用，文件拖入丢弃；Outliner/Details 保留编辑数据只读，Console/提示继续工作，错误进入当前 Editor 日志。关闭窗口先结束 Play，再执行原有未保存确认；停止不依赖运行图像可见性。PIE 与 Editor 同进程，原生崩溃会结束 Editor，进程隔离使用 Standalone Play。

验证入口：project/tests/editor_play_tests.cpp（原生自旋转 Actor、真实 ImGui 控件与输入释放、失败回滚、ST/MT），runtime/tests/renderer_scene_ownership_tests.cpp（两种线程模式的独立注册与释放），editor/tests/editor_play_integration_tests.cpp（复用 Thumbnail target 的真实 Vulkan 首帧准备、暂停/恢复、重复切换和收尾）。实际 Editor docking/高 DPI 交互仍需画面检查。

项目 Actor 出现在 Place Actors 的 Project 分类；Details 由注册属性元数据驱动当前 bool、float32、Vector3 控件，其余字段只读显示。所有参数更新走 Actor validate/apply 与同一连续手势历史；删除恢复也保存 concrete type 和 owned 属性。不能把声明 Edit 自动解释为支持任意反射控件。扩展示例见 project/src，完整边界见 [GameScene](gamescene.md#游戏工程接入边界)。

## 选择、交互与 Undo

### 属性与资源显示

统一显示层放在 Toy3dEditorCore：panels/property_widgets 提供两列属性行、数值/布尔/枚举控件与颜色条；assets/thumbnails/thumbnail_widget 提供所有资产缩略图的边框、类型色条、占位和生成状态。AssetResourcePicker 组合左标签、右缩略图、资源名下拉框与紧凑操作图标；Content Browser 和选择弹窗复用同一缩略图绘制，网格、材质、贴图、骨骼及动画不另设样式。

Content Browser 顶部保持单行靠左排列，Add 下拉菜单、独立 Import 按钮与刷新图标构成操作区，通过分隔线与向上导航、轻量路径面包屑隔开；所有控件按同一行高垂直居中对齐。窄栏路径收为单个截断控件，tooltip 显示完整路径，点击菜单保留所有父目录入口。左侧 Sources 为可折叠目录树，右侧按当前目录显示文件夹和资产卡片，底部显示数量、缩略图大小及 View Options；窄栏隐藏大小滑条并缩短设置按钮，保持单行。Add 集中已有材质/材质实例创建和当前目录导入；Import 先打开原生多选文件窗口，PNG/JPEG 与 HDR 分别进入贴图和环境选项，模型文件先选择 Static Mesh、Skeletal Mesh 或 Animation，再复用已有导入设置。骨骼网格和动画仍限单个源文件，不同导入类别不能混选；取消不创建资产。目录树右键使用点击的目录，资产区空白右键使用当前目录，外部文件拖入共用同一导入路由；选文件后目标目录保持不变。项目根显示 Content，引擎根显示 Engine Content；显示名不改变 /Project、/Engine 虚拟路径。Scene 与其他资产共用列表和选择/打开入口，卡片标为 Level；不创建独立场景目录或移动作者文件。搜索忽略 ASCII 大小写，筛选当前目录中的名称；隐藏引擎内容时返回项目根，目录失效时返回最近有效父目录。Sources 可隐藏，Engine Content 的只读限制保持原有工作流。

实现范围为已有材质参数/预览设置、组件 Details、Actor 已支持反射字段和 World Settings。颜色条点击打开浮点颜色选择器，RGBA 显示透明棋盘格，预览条从线性 RGB 转换为显示色，保存值不改变。控件只负责显示和 ImGui 编辑手势；资源兼容/加载、候选验证、默认/继承和 Undo 仍归调用方。单次调色拖动沿既有连续手势，Escape 取消；只读与加载失败保留原语义。全部 UI 在 GT，不新增 worker、RHI、资产格式、全局服务或 target；跨平台沿现有 ImGui，尺寸从字体高度推导，窄栏截断标签并提供完整 tooltip。

动画窗口的 Preview mesh / Sequence 选择也使用 AssetResourcePicker，仍以当前骨骼 layout 过滤兼容资源；清空分别回到无网格或参考姿态。旧缩略图绘制已删除，资源卡片和主属性面板不保留平行样式入口。验证复用 MaterialUi、Framework、项目 Actor/Play、AnimationPreview 和材质赋值测试，并检查窄栏/高 DPI、拖放、只读、颜色弹窗的取消及 Undo；构建覆盖共享 Toy3dEditor、项目模块 DLL 与 Game。

selection 保存稳定 Actor/Component/Asset ID，使用时解析，删除/切 World 清理失效选择；不长期缓存裸指针。Input 优先级 modal/text → gizmo → viewport → shortcuts → game。

网格、动画和组件材质使用共享 AssetResourcePicker，显示真实缩略图与资源名，支持搜索/过滤、拖放、清空与 Find。大网格/动作由 Editor 持有的 MeshAssetBindings 在后台加载，候选完成并复核后提交一次事务；加载失败保留原资源，PIE 等待该加载结束。详情控件不直接输入资源路径。

一次连续手势合并一条命令，先完整验证候选再 apply；capture/same_state/apply 三者字段一致，不许半条 Transform 已改而后续 light 字段失败。取消恢复起点，撤销/重做恢复完整 component/附着/材质状态。

dirty 以已保存内容身份/分支与外部 content revision 判断，不看 undo 栈深或每帧重新 encode Scene。纯读/viewport render 不污染内容 revision；保存/打开/候选接管等操作在手势结束后执行。

## Scene Save/Open

当前 Scene schema 7、Actor schema 6、Component schema 1，保存 Component 稳定 ID/type/settings、非 root component、跨 Actor attachment、primitive/light 阴影字段与材质 AssetRef。骨骼网格使用独立的类型名分支，保存 mesh/animation 引用和播放设置，不保存瞬时时间或 pose；资源绑定及兼容规则见 [Animation](animation.md#scene-组件资源绑定)。

- Open 在候选 DTO、资产依赖和完整 parent graph/Transform 验证后装配并接管；未知类型/非法环/资源失败保留旧 World/session，registry 注册不等于自动可持久化。
- Save 从 runtime authoritative World 获取完整快照；只在手势结束后，通过 AssetPairStore、同一已验证读取原始 bytes 检查冲突。
- 磁盘 commit 成功后 catalog/UI refresh 失败独立诊断，不能撤销保存事实；失败不清 dirty，Save As/new/copy 的 AssetId 策略一致。
- EmptyActor/非 root 组件等按明确支持类型测试；不靠反射猜创建任意 runtime 对象。

## 场景环境与材质预览

World Settings 选择 Environment 资产或 Off，修改非负 intensity、绕世界 X/Y/Z 的相对旋转，或重置完整 Quaternion。资源先加载验证，再走 World/history 完整候选；连续拖动合并一条命令，Escape 取消，Undo/Redo 和 Scene dirty/保存沿同一历史边界。环境属于 World，PIE 独立装配；运行期间只读。Scene 保存引用、旋转、强度，不保存 GPU 状态。

Tools/Content Browser 的 HDR 导入使用既有 TextureImportDialog worker 生命周期；Texture2D 导入选 Color/LinearData/Normal 与 normal flip-green，Reimport 保留 AssetId 并核对磁盘 baseline。所有作者输入写 project/asset。

骨骼网格、动作导入与 Project 资源的 `Reimport...` 使用独立 CPU worker、GT 冲突复核和逐资产配对发布；Skeleton 选择、源要求、取消/退出与使用入口见 [Animation](animation.md#editor-导入与重导入)。

Content Browser 双击 StaticMesh/Skeleton/SkeletalMesh/AnimationSequence 打开同一 MeshEditorPanel 会话；StaticMesh 显示 `Static Mesh Editor`，其他类型显示 `Animation Editor` 并提供骨骼树、预览网格/动作选择、播放、时间轴、逐样本、root lock 与相机操作。两种网格资产支持默认材质编辑，Skeleton/AnimationSequence 保持只读。交互预览和缩略图各自使用 Renderer-owned 场景，骨骼网格缩略图为参考姿态，动作缩略图拍摄兼容模型的第 0 秒，Skeleton 保留固定骨架图标。行为、兼容校验和生命周期见 [Animation](animation.md#editor-资产预览)。

材质窗口的视口布局、Sphere/Plane/Cube 模型、贴图缩略图参数与交互见 [Material](material.md#可视预览与缩略图)。普通参数即时更新图像，静态选项等待完整候选；Content Browser 的 Material/Instance 缩略图保留固定 studio 配置，不把窗口效果写进主场景。

### 静态网格预览

StaticMesh 与骨骼/动画会话共用 assets/preview/mesh_editor_panel、mesh_preview_asset 和 Toy3dEditorCore；切换类型替换同一会话内容，Renderer 继续使用已有 animation preview 域，不增加目标、线程或图形接口。静态网格保持厘米单位及原始几何，左侧显示网格统计和材质槽，中间提供取景/旋转/平移/缩放，右侧显示公共场景设置；不显示骨骼树和播放控件，不实现碰撞/LOD 编辑。

CPU 在既有 TaskGraph 解码完整配对快照；GT 复核描述及 meta 摘要后接管。StaticMesh 配对解码复用 Core，UI/World/GPU 生命周期沿已有会话，关闭释放场景实例，重开只复用不可变 CPU 输入；加载或拍摄失败保留旧图并诊断。验证覆盖 spider 的真实 GPU 拍摄、类型切换、背景/地面设置、关闭重开和来源失效。既有动画入口迁入通用目录并同步调用方，不保留旧路径。

### 网格材质槽编辑

StaticMesh 与 SkeletalMesh 的 Asset Details / Materials 和场景组件共用 assets/mesh/mesh_material_slots，复用 AssetResourcePicker、property_widgets 与 AssetThumbnailPool。每槽显示 Element 索引、槽名、材质缩略图和资源名，支持选择 Material/MaterialInstance、拖放、定位和清空；清空网格默认赋值恢复引擎材质，清空组件覆盖恢复网格默认材质。当前不增加或重排槽，不导入 FBX 材质/贴图。

assets/mesh/mesh_material_edit_session 由 GT 持有，复用 Core EditSession 的整候选验证和历史；赋值预检通过后更新私有预览，工具栏 Save / Undo / Redo 管理默认材质草稿。切换资产、关闭窗口、退出及切换工程时提示 Save / Discard / Cancel；有草稿时改变预览网格/动作须先保存或丢弃。Engine 网格只读，保存仅写源码侧 project/asset。AnimationSequence 使用所选网格的已保存材质，Edit Mesh Materials 进入对应网格资产，不把材质引用写入动作。

默认材质的 DTO、格式及旧描述迁移见 [Assets](assets.md#网格默认材质)。MeshMaterialEditSession 保留已验证的原始 descriptor baseline 和完整 meta segments；保存检查外部修改，通过 AssetPairStore 发布，几何和未知可选 meta 数据不被重建或丢弃。未知 typed 字段无法完整解码时拒绝编辑；失败保留草稿，磁盘提交成功与 catalog 刷新失败分别报告。

运行材质经 composition root 注入的 MaterialLibrary 解析，load_mesh_materials 统一验证 shader/factory、顶点输入、切线及所需 pass；GT 预检完整候选，失败保留原效果。worker 只解码 owned CPU 输入和材质依赖快照，不访问 Library/UI/World；已有渲染请求完成后才更新预览 World，旧 GPU 使用沿正常 FIFO/drain 释放。草稿不修改共享 Material 对象或主 Scene dirty，不新增 target、线程、单例或 RHI 接口。

保存后的默认材质用于交互预览、缩略图、新放置网格、场景打开及 Game/PIE 资产装配。已装配组件保留其运行网格快照，重新绑定或重新打开场景时读取新默认；显式组件覆盖优先。缩略图和 mesh_preview_asset 记录材质、父材质及纹理依赖的 descriptor（包含 meta 摘要），候选接管/缓存保存复核来源；有默认材质时来源签名包含完整依赖快照。

验证入口为 AnimationPreview ST/MT（两类网格材质草稿、旧描述无写入、冲突、依赖来源失效、真实 GPU 切换/撤销/保存重开和动作继承）、MaterialAssignment（场景覆盖）及 Thumbnail / ThumbnailSource（拍摄、缓存、来源复核）。构建和实际运行证据记录在交付中，不从 CPU fixture 推断所有平台验收。

### 通用资产预览场景

assets/preview/asset_preview_scene 的 AssetPreviewScene 统一实现材质、角色和缩略图的私有 World、灯光及地面，继续归 Toy3dEditorCore。PreviewSceneSettings 只表达环境、主光、背景、地面、阴影和曝光；MaterialPreviewSettings 组合该配置及材质模型/相机，角色相机与播放状态仍归 MeshEditorPanel。preview_scene_widgets 复用 property_widgets 绘制两类窗口相同的 Environment / Lighting / Floor 设置；Reset Scene 仅恢复公共设置，各窗口自己的 Reset Preview / Reset View 管理模型或相机。环境读取经共享 AssetLoader：装配与首帧 cube 经 Critical 有界等待，帧内的环境切换用默认档位轮询，不另建资产管理服务。

`engine/runtime/asset_loader` 的 AssetLoader 供预览窗口异步取得 Environment cube 与贴图：`TextureLoadJob` 在加载线程调用 `build_environment_texture_desc`/`build_texture2d_desc` 产出 owned CPU `TextureDesc`，**Texture 所有权仍在 GT**——GT 在 `AssetLoader::tick()` 的 adopt 阶段执行 `Texture::create` 并缓存（按字节预算淘汰），窗口只消费已就绪的 cube。请求按 asset 身份去重、单任务在途并带优先级（预览环境用 `AssetLoadPriority::High`）；窗口在等待期内保留上一张图或上一帧场景并重试，失败保留旧状态并给出诊断；`invalidate()` 丢弃缓存与在途候选，`invalidate_all()` 用于 Catalog 重扫，`shutdown()` 先等线程退出再释放。首个环境由窗口 `initialize` 内联 `request_texture(..., Critical)` + 有界 `wait()` 取得（私有 World 的首次 `set_environment` 需要 cube），只在 `on_initialize_preview_scene`/`on_initialize_animation_preview_scene` 里调用，不在帧循环；Scene 装配、PIE 与 Game 启动 Scene 用同一档位、经 `load_assembly_texture` 适配器取环境，因此预览窗口刚解码过的环境不会被重复解码。

共用实现与默认 E_PreviewCourtyard、方向光、灰色地面，各窗口保留独立配置、World、Renderer-owned scene/targets、图像身份及 revision。材质与缩略图仍在 Pool 域串行，静态网格与角色仍在独立 animation 域；只在在途请求结束后更新 World，背景/阴影/曝光复制进 PreviewFrameRequest，变更公共设置后过期图像退役。CPU 候选沿既有 TaskGraph，UI/场景配置与接管在 GT，GPU 沿正常 FIFO/submit。环境加载失败、非法设置和 GPU 失败诊断并保留旧图；场景设置自身关闭时撤回请求、退出时 join/drain，不修改主 World、资产 dirty、Undo 或持久化格式。

静态网格与角色保留厘米单位、真实尺寸与独立取景；地面按网格参考姿态下沿及中心定位，地面大小与阴影范围随网格尺寸扩大，播放时不追随脚部。缩略图继续显式采用固定 studio、无背景/地面/阴影的配置，不读取窗口设置。公共 RHI 和每边最多 512 像素读回边界不变，不加入后端专用 API。角色窗口布局及资产兼容行为见 [Animation](animation.md#editor-资产预览)，材质模型与参数行为见 [Material](material.md#可视预览与缩略图)。

验证入口为 AnimationPreview ST/MT 的默认一致性、真实 GPU 背景/曝光/地面变化、失败保旧及切换/关闭；MaterialUi 的共享控件 Reset 与窗口配置隔离；Thumbnail 的固定配置及材质窗口隔离。真实 Editor 另检查高 DPI、侧栏宽度、背景/地面/阴影及退出，未运行的平台不作支持验收。

## 资产缩略图

Content Browser、AssetResourcePicker 和导入流程共用 EditorApplication 持有的 AssetThumbnailPool。固定支持 Material/Instance、Texture2D、StaticMesh、SkeletalMesh、AnimationSequence 及内置 Cube/Plane；Skeleton 使用固定图标，不引入生成器注册框架。材质球、贴图 mip/透明棋盘格、真实网格取景、参考姿态和动画第 0 秒保持既有规则。

生成入口为 assets/texture/texture_preview_image、assets/mesh/mesh_thumbnail、assets/animation/animation_thumbnail 和 assets/material/material_thumbnail；assets/thumbnails/thumbnail_source 统一准备 owned CPU 输入及处理缓存，AssetThumbnailPool 管调度和结果接管，均编入 Toy3dEditorCore。MaterialLibrary 是 GT 发布域，材质解析通过注入的 GT resolver，实时材质窗口会话仍与缩略图共享拍摄场景。

TaskGraph 的 AnyWorker 任务负责网格/动画/贴图读取与解码、骨骼依赖磁盘复核、PNG 编码、保存前内容比较及 Saved 原子写盘。普通 tick/读回回调的 GT 接管只核对当前 catalog 的身份/路径、request 与刷新代次，并准备 World/组件和接管纹理；启动时 studio 环境由 loader 线程解码、GT 只做有界等待，材质 resolver 的读取仍在 GT（先查共享缓存，未命中时同步解码单张贴图）。GPU 完成后，缓存上传候选先异步复核，新拍摄网格/动画候选先复核并保存，成功才替换已有图。CPU 任务只捕获 catalog 副本、owned 输入和 Workspace 生命周期内的文件服务，不访问 UI/World/Proxy；退出先等待任务，再释放输入和场景。SingleThread 映射到 GT，多线程下 GT 等待/Drain 也可能帮助执行 AnyWorker，因此该路由不提供严格的物理后台线程隔离，见 [Threading](threading.md#taskgraph-contract)。

池保持单个在途资产、共享预览场景和 128 条图片容量，没有跨资产流水线。保存、删除等定向变更只失效该身份及其强依赖闭包，其余缓存图片保持可用；工程重扫或手动刷新才整池失效。定向与整池失效同样延迟到下一次 pre-UI tick 生效，帧内已输出的 Image 身份与 RT binding 不变。磁盘 PNG 缓存仅用于网格和动画，材质/贴图保留内存图片。缓存写 /Saved/AssetThumbnails，身份为 AssetId、内容摘要与 thumbnail_generator_version；已在途的旧任务最多产生按旧内容身份命名的可重建缓存，GT 不接管过期结果，不写源码资产。文件/依赖变化、worker 异常和 GPU 失败明确诊断，重新生成失败保留已有图片；刷新会失效旧条目，失败时使用占位及诊断。Windows/macOS 沿已有 FileSystem 和公共 RHI 路径，图像尺寸/字节限制沿 Core PNG contract。

保存或刷新可在 UI 绘制途中请求缩略图失效；池合并请求，在下一次绘制前的 tick 才清除图片并提交纹理退役。当帧已经输出的 Image 命令仍保留注册身份及 RT binding，不能在帧尾快照或绘制前释放它们。

验证入口：tests/thumbnail_source_tests.cpp（CPU 准备、缓存损坏/重载、资产变化冲突、ST/MT、任务退出）、texture_preview_image_tests.cpp（mip、透明背景、损坏输入保旧）、thumbnail_integration_tests.cpp（真实 GPU、绘制中失效与次帧退役、缓存上传、冲突保旧和预览隔离）、animation_preview_tests.cpp（参考姿态/首帧、兼容输入与交互预览并存）。构建与测试名从 engine/editor/CMakeLists.txt 核对。

## 异步与退出

worker 结果带 AssetId/source content/request/session generation；GT 接管前重新确认身份和当前依赖，过期结果释放、不覆盖新窗口。worker 不触碰 UI/World/Proxy；渲染候选/读回遵守逻辑 RT/GPU completion。

退出先禁新请求、cancel/join 工作线程、关闭编辑器 session/preview owner，再 drain 渲染释放；结果和 payload 不能捕获已经销毁窗口。Process/编译流程见 [Material](material.md)，import/asset txn 见 [Assets](assets.md)。

## 开发入口与验证

代表代码 source/scene/components/component_editor_registry.h、assets/asset_editor_registry.h、scene/editor_scene_session.h、scene/editor_command_history.h、scene/placement/actor_factory.h 和 panels。先沿已有同类接入走完整调用链，避免加第二份通用“Editor 接入台账”。

测试 engine/editor/tests/editor_framework_tests.cpp、workspace_tests.cpp、placement_tests.cpp、material_edit_tests.cpp、material_assignment_tests.cpp、texture_preview_image_tests.cpp、thumbnail_integration_tests.cpp。验证字段全链、失败原子性、Undo/Redo/dirty 分支、跨 Actor graph、Save 冲突、过期异步、多窗口资源与退出；UI 行为变化补真实交互/截图，不以纯 DTO round-trip 当界面已验证。

Console 与消息提示见 panels/console_panel.h、editor_notifications.h 和 tests/console_tests.cpp：真实 ImGui 帧验证等级/预设/清空/重开、通知不抢焦点、取消等待、失败保留、日志定位与文件故障恢复，同时核对文件包含被隐藏的记录。Core 的 tests/logging_tests.cpp 验证并发快照、缓冲边界、文件创建/轮转失败与恢复。实际界面测试使用独立临时 .toy 工程，Shader 和 Saved 随工程隔离，不能把测试 Shader 创建到真实工程。
