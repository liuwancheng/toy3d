# Material：属性、继承、渲染与源码迭代

## 定位与模型

core/material 的 Toy3dMaterialAsset 保存 DTO/schema/验证，不含 Program/Proxy/RHI。runtime/rendercore/material 提供 MaterialInterface、Material、MaterialInstance、MaterialRenderProxy、MaterialLibrary。Shader schema 见 [Shader](shader.md)，资产发布见 [Assets](assets.md)，窗口/history 见 [Editor](editor.md)。

MaterialInterface 是当前根/实例共同抽象，不应因旧 RHI 文档曾禁止此名而另建体系。Properties default/schema 来自 ShaderRootMaterial；RT 只消费已解析参数/资源和 Program，不读 YAML/反射或 UI。

## 参数、实例与资源

- 根材质定义有效 schema/默认值；实例仅保存本层 override 与强 parent 身份，多级继承按完整树解析，拒绝 parent cycle/类型冲突。
- Parent 强引用、children 弱引用，MaterialLibrary 共享已保存配置。动态局部修改需明确 clone/实例 ownership，不能修改共享对象使所有用户无意变化。
- 逻辑参数 ID/schema/layout 由 Shader 决定；unknown/name/type/非有限/范围输入失败不改状态、不 enqueue。rename/type change 的旧 override 保留 orphan 并报告，不猜迁移。
- RT 使用稳定 Proxy 和 owned resolved values，一次 FIFO 发布完整状态；GT setter 不直接碰 RHI。高频 binding/cache 不按 native slot 或 Program ID 做资产参数身份。
- 当前材质采样支持已导入 Texture2D 强 AssetRef 与普通 Sampler preset；比较采样器或缺资源不能静默接管。
- Texture view generation 变化使 binding cache 失效，像素更新不是 view 替换；强资源 refs 覆盖所有已提交使用。
- 参数值更改不重编 shader；shader/state/parent graph/资源等结构变更完整候选预检成功再 publish，失败保旧对象/Proxy 地址/效果。

## 编辑、保存与场景赋值

窗口草稿、已保存 DTO、Library 共享运行配置是不同 ownership。预览草稿不污染全场景，保存检查 description 原始字节冲突；成功才清 dirty/update saved configuration，刷新失败另报。

MaterialAssignments 保存场景 AssetRef/命令记录，Library 准备/发布/完成/丢弃配置图。纯材质切换通过 SceneInterface::update_primitive_materials 保留现有 StaticMeshSceneProxy、geometry、HitProxy，不 Remove/Add；旧材质由 FIFO 保活命令/版本 owner 持至安全点。

场景 schema 5 已保存材质赋值，不能宣称场景持久化尚未实现。Material 专用 preview/thumbnail 的后续扩展不能写成已完成。

## 源码登记与外部编辑

EditorApplication 持有 ProcessService、专用 ThreadManager、MaterialShaderWorkflow，并注入窗口/创建框/Assignments；不是全局 Asset cache。

源码 project/shader，清单 project/config/shader_sources.txt 首行 Toy3dShaderSources 1，其余为逻辑名 TAB 相对路径；示例 Project/Surface/Painted 对应 project/shader/painted.shader。

- 含内置源最多256条、清单64 KiB、单源码4 MiB；拒绝重复身份/物理源、越界/symlink escape/非规范路径，Shader 声明名与登记名一致，不能覆盖内置源。
- 不扫描 Content 猜 shader；创建 Material 只列登记源，成功编译后才创建；实例沿根找 source。现有根 shader 改身份需保存/取消草稿，不能强行改名。
- Open Source 使用登记路径；VS Code --reuse-window/--goto，不用 --wait；本机 Editor.CodeExecutable 可配置，找不到明确提示。外部 GUI 属用户，关闭 Toy3d 不杀它。
- Open Error 仅对可解析的根源码位置；include/generated/任意日志路径不直接启动。引擎源为共享实现，项目效果放项目 source。

## 编译、候选与回滚

每请求捕获 Shader identity、活动 AssetId/session generation、source hash；从磁盘保存的源码编译。专用 Thread 同步跑锁定 compiler，120 s、输出1 MiB，一次一个作业，GT poll 完成后 join，退出 cancel/join。

Saved/requests/<随机 AssetId>/ 独占产物，不覆盖旧目录；验证完整 ShaderMapEntry、逻辑名/Pass/profile/default permutation、当前 source/include hash。当前外部 compile-vulkan，D3D 不声称完成。

- ShaderMap 创建不可变候选 Program，不覆写旧 key 缓存；RT 用当前 device/cache/LocalVertexFactory/attachments 预检普通及双面 pipeline，不额外 submit/wait_idle。
- Forward View/Object/lighting ABI 与引擎数据一致，未知 Pass/Global 资源拒绝；GT 只收 owned result，不访问 Proxy/RHI。
- 窗口候选从当前草稿生成，手势结束才接管，保留草稿/history；Library 从已保存 DTO 准备完整共享配置图，不偷读窗口草稿。
- 同一 GT tick：准备所有候选 → FIFO 暂时发布完整图 → 再核 source/include hash 并原子保存请求定位记录 → commit Program/Library → 窗口切 schema/runtime。中途失败在本帧 Draw 入队前用同 FIFO 恢复旧图，旧 refs 仍保活。
- 过期请求、编译/ABI/VF/pipeline/resource 失败不替换旧效果；源码编译不保存 .asset。
- 已发布 Program 当次会话有效，重开加载请求定位记录并重新 GPU 验证；产物缺失/损坏提示重编，不提交生成缓存。内置 ActorFactory 缺省材质随 app 构建加载，当前手动重编刷新已赋值材质资产的场景槽。

## 修改与验证

代码入口 rendercore/material/material.h、material_instance、material_render_proxy；Editor source/material/material_shader_workflow.h。测试 core/material/tests/material_asset_tests.cpp，editor/tests/material_edit_tests.cpp、material_assignment_tests.cpp、material_shader_tests.cpp、material_ui_tests.cpp。

覆盖继承/cycle/孤儿字段、原子 setter、Texture generation、草稿与共享配置隔离、Save 冲突、同 key 新候选、过期请求、真实 compiler 失败、include escape/循环、GPU 预检与回滚、退出取消。测试写 build 隔离目录，不改用户 asset；普通帧不 flush。
