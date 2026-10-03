# Material：属性、继承、渲染与源码迭代

## 定位与模型

core/asset/material 的 Toy3dAssets 保存 DTO/schema/验证，不含 Program/Proxy/RHI。runtime/rendercore/material 提供 MaterialInterface、Material、MaterialInstance、MaterialRenderProxy、MaterialLibrary。Shader schema 见 [Shader](shader.md)，资产发布见 [Assets](assets.md)，窗口/history 见 [Editor](editor.md)。

MaterialInterface 是根/实例共同抽象。Properties default/schema 来自 ShaderRootMaterial；RT 只消费已解析参数/资源和不可变 ShaderMapCollection，不读 YAML/反射或 UI。MaterialDesc::shader_map、MaterialInterface 与稳定 MaterialRenderProxy 持有同一 source/configuration revision 的集合；Program 是集合内可独立查询的结果，不承担其他 factory 的所有权。

## 参数、实例与资源

- 根材质定义有效 schema/默认值；实例仅保存本层 override 与强 parent 身份，多级继承按完整树解析，拒绝 parent cycle/类型冲突。
- Material/MaterialInstance schema 2 增加 `static_options`：每项保存名字与 bool/enum string 的显式 variant kind，最多 32 项。Core 的 `MaterialAssetHierarchy::effective_static_options()` 按 root→leaf 合并，清除本层选项恢复继承；ID 和配置 key 从集合发布的 Shader domain 推导。未知名字、kind 改变、失效 enum 都拒绝加载，不作为普通参数 orphan 保留。普通参数改动不改变该 key。旧 schema 须从源码侧重建。
- Parent 强引用、children 弱引用，MaterialLibrary 共享已保存配置。动态局部修改需明确 clone/实例 ownership，不能修改共享对象使所有用户无意变化。
- 逻辑参数 ID/schema/layout 由 Shader 决定；unknown/name/type/非有限/公共 ScalarRange 越界输入失败不改状态、不 enqueue。rename/type change 的旧 override 保留 orphan 并报告，不猜迁移。
- RT 使用稳定 Proxy 和 owned resolved values，一次 FIFO 发布完整状态；GT setter 不直接碰 RHI。高频 binding/cache 不按 native slot 或 Program ID 做资产参数身份。
- 材质属性采样支持带 Color/LinearData/Normal 要求的 Texture2D 强 AssetRef 与普通 Sampler preset；场景环境 Cube 属于引擎 Pass，不是材质属性；比较采样器或缺资源不能静默接管。
- Texture view generation 变化使 binding cache 失效，像素更新不是 view 替换；强资源 refs 覆盖所有已提交使用。
- 参数值更改不重编 shader；shader/state/parent graph/资源等结构变更完整候选预检成功再 publish，失败保旧对象/Proxy 地址/效果。

## 编辑、保存与场景赋值

窗口草稿、已保存 DTO、Library 共享运行配置是不同 ownership。预览草稿不污染全场景，保存检查 description 原始字节冲突；成功才清 dirty/update saved configuration，刷新失败另报。

MaterialAssignments 保存场景 AssetRef/命令记录，Library 准备/发布/完成/丢弃配置图。纯材质切换通过 SceneInterface::update_primitive_materials 保留现有 StaticMeshSceneProxy、geometry、HitProxy，不 Remove/Add；旧材质由 FIFO 保活命令/版本 owner 持至安全点。

Scene schema 7、Actor schema 6 保存材质赋值及场景环境；旧格式拒绝。材质窗口和 Content Browser 的 Material/MaterialInstance 缩略图使用独立 studio World、有效 MikkTSpace 切线的 S_MaterialPreview 球体和同一生产材质路径，不修改主 World。

## 源码发现与外部编辑

EditorApplication 持有 ProcessService、专用 ThreadManager、ShaderWorkflow，并注入窗口/创建框/Assignments；不是全局 Asset cache。

当前工程 shader 下的 .shader 由 parser 自动发现，读取声明名、Usage Material 和 Role Forward，不按 Pass 显示名推断用途；Editor 发布包含 Forward 及声明的 ShadowDepth/HitProxy roles、全部声明 factories 的完整集合，详见 [Shader](shader.md#项目源码自动发现)。内置用途仍由构建登记，材质统一查询集合，绘制再按 role/factory 取 Program。

- 项目源最多 250 条、单源 4 MiB、有界目录与总字节；重名/非法声明/链接带具体路径诊断，不能覆盖内置源。
- 内置 PBR/Phong/Unlit 与项目源统一查询；创建 Material 只列 Material 用途的登记源，Program 完成 artifact/ABI/GPU 验证后才创建；实例沿根找 source。不扫描 Content 猜 Shader。现有根 shader 改身份需保存/取消草稿，不能强行改名。
- Open Source 使用登记路径；VS Code --reuse-window/--goto，不用 --wait；本机 Editor.CodeExecutable 可配置，找不到明确提示。外部 GUI 属用户，关闭 Toy3d 不杀它。
- Open Error 仅对可解析的根源码位置；include/generated/任意日志路径不直接启动。引擎源为共享实现，项目效果放项目 source。

## 编译、候选与回滚

运行时 `MaterialDesc::static_options` 只保存该节点声明的值；`effective_static_options()` 按 Parent 合并，未声明的值从当前 Shader domain 取默认。`MaterialInstance::create(parent)` 继承；显式配置重载保存该配置的完整选择，带本地选项的重载用于资产层且校验合并后的精确 key。旧默认值不成为临时实例的覆盖项。

Editor 预检由 composition root 收集每个稳定 Proxy 对应的候选配置，Library 预检与发布共用配置图解析，窗口追加其私有草稿用户。RT 只用对应配置验证该用户的 roles、factory 与真实顶点输入，未登记用户明确失败；不把全部配置的属性要求合并到每个 mesh。发布前重新收集并比较用户/配置，SceneInterface 的独立 admission generation 检查 primitive/材质槽/caster role 在预检期间的变化；普通 Transform/骨骼姿态不增加 generation。任一变化拒绝候选并要求重编译，旧发布保持有效。事务回滚包含临时及外部创建的子实例原始集合，不能仅用旧 key 猜回原 source revision。

每请求捕获 Shader identity、活动 AssetId/session generation、source hash；从磁盘保存的源码编译。专用 Thread 同步跑锁定 compiler，120 s、输出1 MiB，一次一个作业，GT poll 完成后 join，退出 cancel/join。

Saved/requests/<随机 AssetId>/ 独占产物，不覆盖旧目录；验证完整集合索引及全部引用的 ShaderMapEntry、逻辑名/Pass/profile/全部所需 Material 与 Pass 配置、当前 source/include hash。索引及不可变集合边界见 [Shader](shader.md#完整集合索引与候选)。Material/Proxy/Library 和编辑器草稿统一接管完整集合，不保留 Local 主程序或 GPU companion。当前外部 compile-vulkan，D3D 不声称完成。

- `MaterialLibrary::prepare_shader(vector<ShaderMapCollectionRef>)` 校验同一源码摘要、profile、domain、feature/policy 和完整 Material schema 的唯一配置集合，总程序不超过 1024。每个已加载资产重新读取已保存继承图，按新声明/default 解析本地静态 override，再精确选择对应集合；缺配置或孤儿静态项拒绝整次准备。publish/discard 对整个图提交或回滚，保持对象/Proxy 身份。单集合重载适用于只有一种所需静态配置的源。Editor 将 default、已保存的每段继承前缀及当前窗口的有效静态草稿一起编译；窗口使用其精确静态配置，不取集合列表的第一项。
- Editor source 持有当前已发布的不可变配置集合，`shader_map(name, selections)` 只规范化并查询内存，缺少配置返回失败，不读取旧部署目录。Core 的资产配置收集在解析继承图之前保存 descriptor 摘要、结束后复查；发布前刷新目录并再次核对，新增/删除/改动材质都会使在途作业失效。engine/project build settings 的 policy、额外配置及 revision 同样参与候选验证。
- ShaderMap::find_or_load_collection 按 Shader/profile/Material permutation 缓存不可变集合；新 revision 从独占目录建立候选，不覆写旧集合。RT 逐 Material 配置及 role/factory 使用当前 device/cache/attachments 预检普通及双面 pipeline，不额外 submit/wait_idle。
- 再收集主场景、预览和 Play 的全部已注册使用者，包含隐藏物体，检查候选 Forward/factory、启用的 mesh roles 与真实顶点布局。几何未就绪返回 NotReady；缺 factory/COLOR0/角色等带 Actor、Component、Section 诊断，保留原集合。
- 新网格创建、override/clear 在 GT 检查 factory、可选颜色和已解析的有效切线要求；组件接入/启用投影检查 ShadowDepth，Editor 可拾取赋值检查 HitProxy，失败不改变原槽。无集合的材质仅表示 CPU/编辑阶段对象，不能据此视为可绘制。
- Forward View/Object/lighting ABI 与引擎数据一致，未知 Pass/Global 资源拒绝；GT 只收 owned result，不访问 Proxy/RHI。
- 窗口候选从当前草稿生成，手势结束才接管，保留草稿/history；Library 从已保存 DTO 准备完整共享配置图，不偷读窗口草稿。
- 同一 GT tick：准备所有候选 → FIFO 暂时发布完整图 → 再核 source/include/资产 descriptor hash 并原子保存整个作业目录的请求定位记录 → commit ShaderMapCollection/Library → 窗口切 schema/runtime。中途失败在本帧 Draw 入队前用同 FIFO 恢复旧图，旧 refs 仍保活。
- 过期请求、编译/ABI/VF/pipeline/resource 失败不替换旧效果；源码编译不保存 .asset。
- 已发布集合当次会话有效，重开加载请求定位记录并重新 GPU 验证；产物缺失/损坏提示重编，不提交生成缓存。内置 ActorFactory 缺省材质随 app 构建加载，其共享 root 加入 Library 同一配置图事务，重编刷新资产槽与默认几何，保持 Proxy/geometry 身份。

无可用集合时赋值返回资产路径、Shader 身份与登记/验证/失败原因。Compile and Assign 捕获 scene generation、Actor/Component/slot、mesh/旧材质和继承链文件摘要；成功后重新验证目标与文件，再走原 Undo 命令。目标改变、编译失败或过期都保原槽，不自动保存材质。

## 静态选项编辑

`MaterialEditSession::open` 同时接收完整 Material 参数 schema 与已发布的 Material static domain。窗口只显示该声明的 bool/enum；`set_static_option`/`remove_static_option` 通过既有 EditSession 的字段 patch 保存本层选择，验证完整继承结果并支持 Undo/Redo。清除本层值恢复 Parent 或 Shader default，不将继承值复制到资产。未知名称、kind 改变、失效 enum 和源码删除已使用维度都是错误；更新 domain 失败保留旧声明和草稿。

Panel 修改静态选项时保留旧预览，收集当前草稿并发起完整 source job。静态撤销/重做推进会话 revision，在途旧草稿候选无法接管；忙时合并为一次最新草稿重编译。普通参数编辑沿原来的数据更新路径，不因颜色/标量变化推进静态编译身份。Parent 切换同时更新其参数 schema、domain 与有效静态继承。

窗口保存（包括关闭时 Save）要求当前静态配置已经通过候选验证并由预览接管，避免将未验证配置写入后再使共享图加载失败。草稿/历史仍属于窗口；成功保存后按既有 Library 的资产图发布，不把窗口的可变预览共享给场景。Shader Capabilities 显示只读的 factory 和 feature 支持，用户不直接选择 engine Pass permutation。

## 绘制查询与参数绑定

Base Pass 从材质集合精确查询 Forward + 当前 VertexFactory；已声明的 ShadowDepth/HitProxy 从同一材质集合精确查询，缺 factory 不换用其他程序。编译后 Standard Opaque contract 可复用独立内置默认深度/拾取集合，不按 Shader 名称特判。Standard Masked 必须有完整三角色，Custom 必须提供所请求的角色。

MaterialRenderProxy 按 active Material group identity 缓存绑定；拥有相同 active 声明的角色/factory 复用一份绑定，native slot 或 Program 地址不参与持久缓存身份。`materialize(device, context, program)` 要求程序属于当前不可变集合，按完整 schema 核对后只编码 active 常量缓冲及资源；无 Material 使用时返回空绑定。无 program 的重载仅准备默认 Forward（优先 Local，源仅支持 GPUSkin 时使用 GPUSkin），生产绘制传实际选中的程序。

相机 Forward/自定义 HitProxy、离屏 ShadowDepth 在 render pass 开始前分别准备所需程序；MeshBatch 持有程序与绑定的强引用快照。未使用的 texture 默认可为空，仍保存完整属性声明；显式资产 override 无论是否 active 都必须解析，不能隐藏损坏引用。普通纹理更新只使使用该参数的缓存失效；同 view 的像素更新不改 binding，view/generation 变化重新建立快照。常量缓冲保持完整 ABI，数值变化不重新打包偏移。

候选独立预检全部角色/factory/Pass 配置的 Material 绑定，相同 active 声明与值可复用旧绑定；新增必需资源不就绪则失败并保留当前状态。提交同时切换集合、two_sided 和完整缓存，删除该配置不再使用的布局；旧 GPU 使用通过已有强资源 refs 保活。每个 Pass 的状态来自实际查询的 Program，再叠加实例 two_sided。

## 可视预览与缩略图

MaterialEditorPanel 的私有 runtime 随参数 revision 请求最新预览；静态候选成功才切换 runtime，编译期间保留旧图。AssetThumbnailPool 把 Material/Instance 缩略图与 live preview 排入一个 Renderer preview scene，串行使用同一设备、上下文及提交路径。已有缩略图任务先处理，live request 合并到最新值；窗口暂停请求后不持续刷新。地面组件在首次需要显示时注册，启动阶段的 Shader 验证不依赖尚未请求的预览帧来上传几何。

窗口默认使用 E_PreviewCourtyard HDR、方向光与独立灰色地面。Preview Scene 可选择引擎/项目 Environment 或 Off，调整环境强度/绕 Y 旋转、主光颜色/方向/强度及固定曝光，分别切换背景、地面和阴影；背景显隐不关闭镜面 IBL，Off 才撤回环境。拖动图像环绕相机，滚轮缩放，Reset Preview 恢复默认设置。设置属于窗口会话，不参与材质 dirty/history 或主 World；图像按窗口宽度在 192..512 范围量化渲染，服务接受每边 96..1024 的有界尺寸。

缩略图仍使用固定 studio、固定小尺寸和无地面/阴影的轻量配置。GT 仅在串行队列空闲后切换私有 World 配置，Draw 携带不可变设置；背景采样同一环境的 mip 0，反射采样 GGX mip，同步强度/旋转，没有球谐或环境漫反射。图片仅在实际 GPU readback 成功且材质/设置 revision 仍匹配时接管；过期结果退役，失败保留旧图并诊断。关闭/替换私有 runtime 前撤回 mesh、取消未发送请求、退役 UI texture，并 drain FIFO 后释放最终 Material owner；普通帧不 flush。缩略图 generation 3，asset reload 使缓存与窗口环境候选失效，不将窗口草稿写成已保存资产。

## 修改与验证

代码入口 rendercore/material/material.h、material_instance、material_render_proxy；Editor source/shader/shader_workflow.h。测试 core/tests/material_asset_tests.cpp，editor/tests/material_edit_tests.cpp、material_assignment_tests.cpp、material_shader_tests.cpp、material_ui_tests.cpp。

覆盖继承/cycle/孤儿字段、原子 setter、Texture generation、草稿与共享配置隔离、Save 冲突、同 key 新候选、过期请求、真实 compiler 失败、include escape/循环、GPU 预检与回滚、退出取消。测试写 build 隔离目录，不改用户 asset；普通帧不 flush。
