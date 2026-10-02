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

EditorProject::create 先在父目录的独占 staging 建目录/配置，最后写 .toy，再以 no-replace 重命名发布；已存在目标不覆盖。普通失败只清理本次独占 staging，不承诺断电时的目录发布持久性。新工程默认继承引擎场景，资源目录为空。项目 Editor 仅承载自身已链接的一个 Runtime 模块，资源工程使用 Toy3dEditor；不生成 C++ 模板或自动构建。

首版一个进程关联一个工程，切换启动目标 Editor 实例；继承显式命令行配置覆盖。后台编译/Import 完成或取消后才切换，材质/场景 dirty 依次确认；launch 接受后关闭当前窗口，launch 失败保持会话。启动进程成功不等于完成 GPU/资源初始化，不作跨进程 ready 保证。Project Settings 保存 Editor/Game 默认场景覆盖并显示当前有效值来源，下次启动生效。

验证入口 project_tests.cpp、editor_framework_tests.cpp、material_shader_tests.cpp；UI 模态期间屏蔽修改快捷键。界面、默认场景装配、未保存切换仍需真实交互验证，CPU DTO 测试不能替代画面验证。

Scene > Standalone Play 仅在关联已构建 Game 的项目 Editor 中可用，等待启动验证/编译/Import/属性手势完成。捕获当前场景到工程 Saved/play/<新 ID>.scene，再独立启动 Game；无需保存编辑中的场景，不改变作者文件、场景身份、dirty 或撤销历史，运行状态不回写。启动通知仅说明进程接受启动，失败详情看工程 Saved/logs 的 game 日志；无跨进程 ready/停止按钮，关闭 Game 窗口结束运行。快照是可删除缓存，关闭 Game 后可清理 Saved/play。

### 视口内 Play

Scene Viewport 右上角提供 Play/Pause/Resume/Stop 图标按钮，悬停显示动作名，保留原 docking 窗口身份。无工程/资源工程可运行内置 Actor；项目 Actor 使用当前 Editor 已链接、已注册的模块，不依赖 Game 可执行文件。首版单个会话，不含 Simulate/Eject、多客户端、运行状态回写或热重载。

- source/scene/EditorPlaySession 属 Toy3dEditorCore，由 EditorApplication 持有。启动捕获当前未保存内容到内存 SceneAssetData，复用 assemble_scene 和冻结注册表，不落盘。运行 Actor/Component、Mesh 渲染数据、MaterialLibrary 与可变材质实例独立；编辑对象、选择、history、dirty 和观察相机保持原样。不能仅以两个 World 中相同的 Actor ID 认定对象相同。
- Renderer 提供独立 Play RenderScene，Engine/Application 帧提交选择对应 SceneInterface，复用主视口附件和 RHI。编辑渲染注册始终保留；移除最后一个 Mesh Proxy 会终结 Mesh 资源生命周期，不能反复解绑/重绑编辑 World，也不能跨 RenderScene 共用这种 MeshRenderData。Preview Scene 留给缩略图。Game/PIE 共用 gamescene/scene_view.h 的相机规则，停止恢复编辑观察相机。
- GT 帧边界执行状态转换。Starting 等待逻辑 RT 检查资源并成功提交场景帧后才 begin_play，等待时间不计入游戏时间；30 秒没有就绪帧则撤回候选并诊断。SceneRenderFeedback 为每次启动独立的共享反馈，release/acquire 发布结果，Ready 不代表 GPU 已完成。Playing 只 tick 运行 World，Paused 不推进 tick/时间但继续绘制。
- 停止先释放输入、end_play/解绑/销毁运行 World，再 drain RT 撤回命令、关闭 MaterialLibrary、释放 geometry；GPU 生命周期仍遵守既有资源引用保留规则。启动与停止清理拾取请求，旧反馈不能接管新会话。点击运行图像获取输入，Shift+F1、焦点丢失、文本/modal 或暂停释放，Esc 停止；ImGui 帧尾后设置有效捕获策略，事件和键盘 Hold 不绕过捕获。运行期间新建的 InputBindingContext 在停止后移除，项目代码不得覆盖宿主已有 context。
- 运行期间 Scene/资产编辑、Undo/Redo、导入、Shader 发布和工程切换禁用，文件拖入丢弃；Outliner/Details 保留编辑数据只读，Console/提示继续工作，错误进入当前 Editor 日志。关闭窗口先结束 Play，再执行原有未保存确认；停止不依赖运行图像可见性。PIE 与 Editor 同进程，原生崩溃会结束 Editor，进程隔离使用 Standalone Play。

验证入口：project/tests/editor_play_tests.cpp（原生自旋转 Actor、真实 ImGui 控件与输入释放、失败回滚、ST/MT），runtime/tests/renderer_scene_ownership_tests.cpp（两种线程模式的独立注册与释放），editor/tests/editor_play_integration_tests.cpp（复用 Thumbnail target 的真实 Vulkan 首帧准备、暂停/恢复、重复切换和收尾）。实际 Editor docking/高 DPI 交互仍需画面检查。

项目 Actor 出现在 Place Actors 的 Project 分类；Details 由注册属性元数据驱动当前 bool、float32、Vector3 控件，其余字段只读显示。所有参数更新走 Actor validate/apply 与同一连续手势历史；删除恢复也保存 concrete type 和 owned 属性。不能把声明 Edit 自动解释为支持任意反射控件。扩展示例见 project/src，完整边界见 [GameScene](gamescene.md#游戏工程接入边界)。

## 选择、交互与 Undo

selection 保存稳定 Actor/Component/Asset ID，使用时解析，删除/切 World 清理失效选择；不长期缓存裸指针。Input 优先级 modal/text → gizmo → viewport → shortcuts → game。

一次连续手势合并一条命令，先完整验证候选再 apply；capture/same_state/apply 三者字段一致，不许半条 Transform 已改而后续 light 字段失败。取消恢复起点，撤销/重做恢复完整 component/附着/材质状态。

dirty 以已保存内容身份/分支与外部 content revision 判断，不看 undo 栈深或每帧重新 encode Scene。纯读/viewport render 不污染内容 revision；保存/打开/候选接管等操作在手势结束后执行。

## Scene Save/Open

当前 Scene/Actor schema 6，保存 Component 稳定 ID/type/settings、非 root component、跨 Actor attachment、primitive/light 阴影字段与材质 AssetRef。

- Open 在候选 DTO、资产依赖和完整 parent graph/Transform 验证后装配并接管；未知类型/非法环/资源失败保留旧 World/session，registry 注册不等于自动可持久化。
- Save 从 runtime authoritative World 获取完整快照；只在手势结束后，通过 AssetPairStore、同一已验证读取原始 bytes 检查冲突。
- 磁盘 commit 成功后 catalog/UI refresh 失败独立诊断，不能撤销保存事实；失败不清 dirty，Save As/new/copy 的 AssetId 策略一致。
- EmptyActor/非 root 组件等按明确支持类型测试；不靠反射猜创建任意 runtime 对象。

## 异步与退出

worker 结果带 AssetId/source content/request/session generation；GT 接管前重新确认身份和当前依赖，过期结果释放、不覆盖新窗口。worker 不触碰 UI/World/Proxy；渲染候选/读回遵守逻辑 RT/GPU completion。

退出先禁新请求、cancel/join 工作线程、关闭编辑器 session/preview owner，再 drain 渲染释放；结果和 payload 不能捕获已经销毁窗口。Process/编译流程见 [Material](material.md)，import/asset txn 见 [Assets](assets.md)。

## 开发入口与验证

代表代码 source/scene/components/component_editor_registry.h、assets/asset_editor_registry.h、scene/editor_scene_session.h、scene/editor_command_history.h、scene/placement/actor_factory.h 和 panels。先沿已有同类接入走完整调用链，避免加第二份通用“Editor 接入台账”。

测试 engine/editor/tests/editor_framework_tests.cpp、workspace_tests.cpp、placement_tests.cpp、material_edit_tests.cpp、material_assignment_tests.cpp、texture_preview_image_tests.cpp、thumbnail_integration_tests.cpp。验证字段全链、失败原子性、Undo/Redo/dirty 分支、跨 Actor graph、Save 冲突、过期异步、多窗口资源与退出；UI 行为变化补真实交互/截图，不以纯 DTO round-trip 当界面已验证。

Console 与消息提示见 panels/console_panel.h、editor_notifications.h 和 tests/console_tests.cpp：真实 ImGui 帧验证等级/预设/清空/重开、通知不抢焦点、取消等待、失败保留、日志定位与文件故障恢复，同时核对文件包含被隐藏的记录。Core 的 tests/logging_tests.cpp 验证并发快照、缓冲边界、文件创建/轮转失败与恢复。实际界面测试使用独立临时 .toy 工程，Shader 和 Saved 随工程隔离，不能把测试 Shader 创建到真实工程。
