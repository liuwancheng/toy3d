# Editor：框架、接入与编辑一致性

## 定位与 ownership

engine/editor/source，程序 Toy3dEditor；可复用库 Toy3dEditorCore。功能目录为 scene（history/selection/placement/components）、viewport、assets（material/texture/mesh/thumbnails）、panels、workspace、platform；资产专属窗口放对应 assets 子目录，通用面板注册留 panels。EditorApplication 是 GT composition root，持 Workspace、selection/history、registries、异步工作流并明确退出；runtime/core 不依赖 ImGui/Editor。

Engine 拥有主 World，EditorSceneSession 管描述身份、读取/装配/保存状态，不复制另一套可变 World。Editor 不启动主 World gameplay；preview World 独立。文件创作写 project/asset，Engine 资产只读，Saved 仅缓存，不写 bin。

## 接入：面板、组件与资产

- PanelRegistry 统一 draw/menu/focus/shortcut；每个面板/全局动作每帧恰好一次，不分散处理 Ctrl+Z/Y/S。窗口状态归实例，不用 static 跨会话共享。
- ComponentEditorRegistry 用显式 create/capture/apply/Details；runtime RTTI 只用于对应关系，不成为持久化身份。新增组件不在中央累计 dynamic_cast 分支。
- 新字段闭环：settings 整体验证 → setter/content revision → DTO/反射/schema/codegen → registry capture/apply/Details → 手势/history → Scene 装配/Save/Open → 测试。UI metadata 不代替完整候选校验。
- AssetEditorRegistry 按 root_type 注册，不只按扩展名；窗口保 own session/dirty/history，候选读取失败保持旧 preview/session，不能先清空再加载。
- Texture preview 等资源须 GPU-ready 才替换显示；Editor 不保 Vk* 或私建 device/view ownership。

## 选择、交互与 Undo

selection 保存稳定 Actor/Component/Asset ID，使用时解析，删除/切 World 清理失效选择；不长期缓存裸指针。Input 优先级 modal/text → gizmo → viewport → shortcuts → game。

一次连续手势合并一条命令，先完整验证候选再 apply；capture/same_state/apply 三者字段一致，不许半条 Transform 已改而后续 light 字段失败。取消恢复起点，撤销/重做恢复完整 component/附着/材质状态。

dirty 以已保存内容身份/分支与外部 content revision 判断，不看 undo 栈深或每帧重新 encode Scene。纯读/viewport render 不污染内容 revision；保存/打开/候选接管等操作在手势结束后执行。

## Scene Save/Open

当前 Scene/Actor schema 5，保存 Component 稳定 ID/type/settings、非 root component、跨 Actor attachment、primitive/light 阴影字段与材质 AssetRef。

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
