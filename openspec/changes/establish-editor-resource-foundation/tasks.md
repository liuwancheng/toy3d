# Tasks

## 1. 共享方案与构建边界

- [x] 1.1 将本 change 的用例与非目标、目录和 CMake target、接口与实现分层、所有权与生命周期、线程、错误、平台、安全、测试矩阵、迁移顺序和旧入口删除条件写入 `document/` 的 Active 专项设计；在 `document/index.md` 登记，并核对其与 proposal、design 和五份 spec 一致。
- [x] 1.2 盘点 `engine/core/`、现有生成工具、文件系统、日志、数学及相关 CMake 入口，固定 `Toy3dReflection`、`Toy3dSerialization`、`Toy3dResource` 和 `reflection_codegen` 的依赖方向与离线工具供应方式；用 CMake 配置确认不下载依赖、不从 tools 反向依赖 runtime。
- [x] 1.3 建立三个独立第一方目标和生成器目标的最小构建骨架，生成头文件只写入构建目录；验证 Windows Debug 配置与受影响目标构建成功，并检查没有在 `engine/asset/` 放 C++ 系统代码或改动既有资产路径。

## 2. 反射声明、生成与注册

- [x] 2.1 固定跨 MSVC/Clang 的 opt-in 标记语法、稳定类型/字段名、schema 版本及受支持字段清单；实现生成器对明确头文件输入清单的解析，并以小型 DTO fixture 验证生成登记与编解码声明。
- [x] 2.2 实现 `Edit`、`Visible`、`Transient` 及 `Category`、`Range`、`Unit`、`AssetType` 的描述生成；验证未标记字段不出现、无用途标记字段默认可保存、`Visible | Transient` 只读且不保存，以及非法组合报告源文件、类型与字段。
- [x] 2.3 实现 `TypeRegistry` 的显式模块注册、冲突检查、freeze 与只读查询；用测试验证重排 C++ 字段不改变持久化身份、重复名和不一致描述失败、冻结前及失败注册不暴露半成品，并发读取观察到同一描述。
- [x] 2.4 为固定宽度值、UTF-8 文本、稳定枚举、Core Math 值、嵌套结构、数组及受控 tagged variant 生成类型描述；测试原始指针、未知元素类型、未知分支及不可访问字段均在生成或注册阶段明确失败。
- [x] 2.5 将反射入口、标记语义和生成头文件接入方式写入 Active 专项设计及 `document/core-module-usage-index.md`；核对示例在当前 CMake 目标下可编译，不依赖静态初始化顺序。

## 3. 值序列化与反序列化

- [x] 3.0 将文件系统中重复的 UTF-8 有效性检查抽到独立 `Toy3dText` 目标，路径策略仍留在 FileSystem；用现有 FileSystem 测试与新增文本边界测试验证行为一致，并更新 Active 文档与 Core 使用索引。
- [x] 3.1 在 `Toy3dSerialization` 中实现有界 reader/writer 和确定性的基本值编解码，规定字节序、数值范围、非有限浮点、UTF-8、深度、数组与总字节限制；以往返和畸形输入测试验证每种限制返回字段位置与原因。
- [x] 3.2 实现基于冻结 schema 的嵌套结构、数组与 tagged variant 双向编解码，默认读写显式声明且非 `Transient` 的创作属性；测试字段声明顺序与无序数据插入顺序不改变规范化输出，未知必需分支不被误解码。
- [x] 3.3 建立类型/字段 schema 版本迁移入口与显式字段改名、类型转换步骤；用 `half_extent` 到 `half_extents` fixture 验证旧文件能迁移，新版本与非法截断失败且原始输入不被覆盖。
- [x] 3.4 规定未知可选字段的原样保留能力与不可无损保存时的只读失败路径；验证旧 reader 不会静默丢失新字段，未知必需字段拒绝消费，并把编解码与错误返回用法写入 Active 专项设计和 Core 使用索引。

## 4. Asset 文件容器与可靠保存

- [x] 4.1 固定 Toy3d Asset 文件的 magic、格式版本、字段字节序、段目录、索引和数据段编码 fixture，不直接写 C++ struct 内存布局；验证跨不同宿主字节序的预期字节和版本识别。
- [x] 4.2 实现 `inspect_asset()` 只读取头、依赖/子资源索引及所需段目录；以含大型数据段的 fixture 验证不解码完整类型数据，并拒绝越界、重叠、超限与未知必需段。
- [x] 4.3 实现 `load_asset<T>()` 的根类型检查、文件格式迁移、类型化解码与完整候选验证；用错误注入验证失败时调用方原有资源不变，错误携带 Asset ID、`VirtualPath` 和字段或段位置。
- [x] 4.4 实现 `save_asset<T>()` 与 `FileSystem::write_binary_atomic()` 的完整候选发布，并检查写入、flush、close 和替换结果；测试短写与替换失败时旧文件仍可读、保存返回失败。
- [x] 4.5 实现类型数据段与可选大块段或 blob 引用的边界，无法无损保留未知可选段时禁止覆盖保存；用模型大段 fixture 验证可读取创作数据与索引而无需把顶点展开成属性树，并在 Active 专项设计记录段扩展规则。

## 5. Asset 身份、索引与引用

- [x] 5.1 实现稳定 `AssetId`、`SubresourceId`、`AssetRef`、其值编解码适配与由 owner 持有的 ID→`VirtualPath`/类型索引，保持 `Toy3dSerialization` 不依赖资源目标；测试身份值往返、文件移动后引用仍解析、重复 ID 和不合法路径报错，以及任意 `engine/asset/` 子目录都不影响类型判断。
- [x] 5.2 实现从 Asset 文件索引枚举直接依赖与类型化引用检查，不由通用编解码层加载运行时对象；测试缺失资源、预期/实际类型不符和子资源缺失时返回引用位置。
- [x] 5.3 实现强依赖加载的循环检测和显式弱/延迟引用边界；用 A→B→A fixture 验证报告循环路径且不会无限递归。
- [x] 5.4 提供导入结果与旧子资源身份的匹配接口和 orphan 报告，不按数组下标重绑；测试模型节点重排保持 mesh 覆盖目标、子资源消失保持原引用并报错，并在 Active 专项设计记录后续导入器责任。

## 6. 无界面编辑事务

- [x] 6.1 实现类型化资源快照、属性路径与稳定作者元素 ID，支持嵌套结构、数组和受控变体读取；测试在动画事件前插入元素后选择与撤销目标仍指向原事件。
- [x] 6.2 实现单次及复合字段 `apply_edit()` 的用途、类型、引用和领域验证，成功后原子发布 old/new 撤销记录；测试只读属性、非法引用和 near/far 联合修改，失败不改变快照、撤销栈或预览通知。
- [x] 6.3 实现 `EditSession<T>::undo()`/`redo()`、资源脏状态及 `save()`；测试编辑后脏、成功保存后清除，保存/迁移/重导入冲突失败时编辑值和撤销记录仍可恢复。
- [x] 6.4 实现领域化变更类别和假预览适配器，区分普通 setter 更新与重新导入、Cook 或完整候选替换；测试预览只在成功事务后收到通知，失败候选保持旧预览，通用层不触碰 RHI、RenderProxy 或 Actor 生命周期。
- [x] 6.5 在 Active 专项设计写明编辑会话所有权、线程和错误由调用方转为现有 Logger 或 Editor Dialog 的位置；核对实现没有独立诊断系统及每层重复日志。

## 7. 跨资源类别的代表性验证

- [x] 7.1 添加 `ModelAssetData` fixture，覆盖源路径、导入设置、mesh/material/skeleton 子资源覆盖和独立几何段引用；验证往返、任意资产子目录、节点重排与缺失 Cook 产物的明确错误，不解析外部模型文件。
- [x] 7.2 添加 `AnimationAssetData` fixture，覆盖骨架引用、稳定轨道目标、key、插值和事件；验证往返及时间排序、范围、非有限值与目标缺失时的准确位置。
- [x] 7.3 添加 `CollisionAssetData` fixture，覆盖 box、sphere、capsule、convex/triangle mesh 引用与局部变换；验证 tagged variant 往返，非正 capsule 半径和未知必需形状失败，且不构造物理后端对象。
- [x] 7.4 添加 `SceneAssetData` fixture，覆盖 Actor/Component ID、类型、属性、root、跨 Actor attachment 和外部引用；验证身份唯一、两端存在、附件无环，失败不发布候选且不调用 `spawn_actor<T>()`。
- [x] 7.5 添加材质覆盖与 `.shader Properties` 的只读 schema 适配 fixture；验证参数身份与类型以 Shader schema 为准，orphan 覆盖不按槽位改绑，假适配器不直接写 RenderProxy，并在 Active 专项设计标出后续生产适配器范围。

## 8. 集成核对

- [x] 8.1 运行受影响目标在 Windows 的配置、构建与已登记测试，并核对生成代码只在 build 目录；在可用的 macOS/Clang 环境完成同等配置构建，无法运行的平台记录未验证范围。
- [x] 8.2 运行 OpenSpec 严格校验，核对 proposal、design、specs、tasks 与 `document/index.md`、Core 使用索引的名称、职责和 API 调用链一致；确认本 change 未引入生产导入器、Cook、Editor UI 或 GameScene 装配入口。
