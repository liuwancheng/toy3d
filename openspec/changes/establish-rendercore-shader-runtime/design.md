## Context

动机见 [proposal.md](./proposal.md)。当前实现已经具备两个可复用基础：进程级 `ShaderMap` 负责 CPU Program 的加载、严格验证与缓存；`rendercore/shader/rhi_shader_program.*` 已能把一个 `ShaderMapProgram` 转换并创建为 RHI stage/layout 聚合。但后者没有 device-scoped reuse，调用者仍可逐帧直接创建。

职责错位主要集中在三条调用链：

- `forward_scene_renderer.cpp` 在匿名 namespace 中翻译全部 Shader graphics state，非法枚举以默认值继续；Base Pass 在逐帧 draw 路径创建 RHI Program。
- `engine.cpp` 同时决定内置 Shader 字符串 key、写死 Vulkan Shader platform、执行加载并逐项保存 Tonemap/ImGui Program。
- Tonemap/ImGui 再次验证 shader/pass/platform identity，并分别调用 RHI Program 创建入口。

设计必须服从现有 Active Shader/RHI contract：`ShaderMap` 是 CPU Program 唯一加载与验证入口；`ShaderPlatform` 不进入公共 RHI；Vulkan、D3D11 FL11_0/SM5、D3D12/SM6 使用同一上层语义；RHI object 只在 logical RT 创建和销毁；显式 SceneRenderer/业务 Pass 保留，RDG 与通用 Pass Scheduler 不在本阶段引入。

## Goals / Non-Goals

**Goals:**

- 建立清晰的三层模型：通用 CPU `ShaderMap`、内置 Shader 领域 `GlobalShaderMap`、device-scoped `RHIShaderProgramCache`。
- 让一个新内置 Pass 只声明一个 `GlobalShaderType` 并加入 Renderer 启动所需集合，不再修改 Renderer 构造签名或复制 Shader/RHI 创建逻辑。
- 把 Shader format→RHI graphics state 翻译收口到 RenderCore，并让未知值显式失败。
- 先消除 Base Pass 的逐帧 RHI Program 创建，同时为后续 `MaterialShaderMap` 预留同一 cache 路径。
- 给出可按小批次实施、每批都能删除旧入口并独立验证的目录与迁移顺序。

**Non-Goals:**

- 不把 Shader Program cache 放入公共 `RHIDevice`；它理解 `ShaderMapProgram`，属于 RenderCore 而非 RHI。
- 不实现 UE 的单 stage shader type、`IMPLEMENT_GLOBAL_SHADER` 宏、静态初始化 registry、shader type hierarchy 或全局单例。
- 不在本 change 建立 `MaterialShaderMap`、Mesh Pass Processor、PSO precache、热重载、异步 Shader I/O 或 concurrent cache miss。
- 不拆分 `Toy3dRuntime` CMake target，也不为了目录观感迁移现有 RenderScene 文件。

## Decisions

### 1. 固定三层 Shader runtime，并禁止反向依赖

```text
GT / process CPU domain
ShaderMapLoader -> ShaderMap -> GlobalShaderMap (frozen)
                              |
                              | shared immutable ShaderMapProgram refs
                              v
logical RT / Renderer device domain
RHIShaderProgramCache -> RHIShaderProgram -> RHIDevice public create APIs
                              |
                              v
Tonemap / ImGui / Forward Base Pass
```

- `ShaderMap` 继续按完整 `ShaderMapProgramKey` 执行 I/O、Entry validation、parameter index 与 CPU reuse。
- `GlobalShaderMap` 只增加“哪些 Program 是引擎内置 type”的领域索引和 type contract validation，不复制 loader 或 Program storage。
- `RHIShaderProgramCache` 只增加当前 device 上的 stage/layout object reuse，不决定 ShaderPlatform、Pass 选择或文件位置。
- Pass 只消费查询结果。Pass 不拥有 loader/map/cache，也不判断 backend。

选择该分层是因为 CPU Program 可以跨 device 生命周期存在，而所有 RHI refs 必须晚于 device 创建、早于 device 销毁。把两者合并会迫使 GT 持有 RHI 可变状态，或迫使 RT 执行同步文件 I/O。

替代方案是扩展 `RHIDevice` 自带 ShaderMap cache；否决原因是公共 RHI 会反向依赖 Shader format 与 runtime `ShaderMap`，D3D/Vulkan backend frontend 也会承担不属于 RHI 的资产 identity。

### 2. 目录按“通用机制”和“Pass 声明”分开，不新建 RenderCore target

本 change 采用以下文件布局：

```text
engine/runtime/rendercore/shader/
├── global_shader_type.h                 # GlobalShaderType、binding requirement
├── global_shader_map.h/.cpp             # GT build、freeze、typed lookup
├── rhi_shader_program.h/.cpp             # 现有 Program desc/value 与无缓存转换
├── rhi_shader_program_cache.h/.cpp       # RT、device-scoped cache
└── shader_graphics_state.h/.cpp          # Shader state -> RHI pipeline state adapter

engine/runtime/renderscene/
├── renderer_builtin_shaders.h/.cpp       # 组装本次 required type 指针集合
├── postprocess/tonemap_pass.h/.cpp        # Tonemap type 声明与 Pass 实现同域
├── ui/imgui_renderer.h/.cpp               # ImGui type 声明与 UI 实现同域
├── renderer.h/.cpp                        # 冻结 map 输入、cache owner、bootstrap
└── view/forward_scene_renderer.cpp        # 只组合 adapter/cache，不再定义转换

engine/runtime/tests/
├── global_shader_map_tests.cpp
├── rhi_shader_program_cache_tests.cpp
├── shader_graphics_state_tests.cpp
└── 现有 renderer/tonemap/imgui/forward tests（按行为更新）
```

通用 Global Shader 抽象属于 RenderCore；具体 type 声明按 UE 的职责思路放在消费它的 renderer feature 附近。`renderer_builtin_shaders.*` 只是 composition helper：返回显式 type 指针集合，不保存状态、不注册类型、不成为 manager。这样新增 Copy/Depth pass 时，声明跟随 Pass，启动选择仍有一个可搜索入口。

本阶段继续使用现有 `Toy3dRuntime` target。当前 CMake 已递归收集 runtime 源码，新测试仍需显式登记独立 target。等 RenderCore API 和依赖稳定且确有 editor/tools 复用需求时，再单独提出 target 拆分；本 change 不把目录调整伪装成模块隔离。

替代方案是建立 `rendercore/shader/global/tonemap/` 等集中目录；否决原因是它会把具体 Pass 的 schema 与维护责任从消费模块抽走。另一个方案是把通用类型放进 `renderscene/global_shader/`；否决原因是后续 Material 与其他 RenderCore 使用者会被迫依赖 RenderScene。

### 3. `GlobalShaderType` 是显式多 stage Program descriptor

`GlobalShaderType` 是不可变值，至少包含：

- 稳定且进程内唯一的 `type_name`，用于 map lookup 与诊断；
- shader name、pass name 和默认/显式 permutation key，但不内嵌 `ShaderPlatform`；
- exact required stage flags，当前 Tonemap/ImGui 均为 vertex+pixel；
- 一组 `GlobalShaderBindingRequirement`，以 `ShaderParameterId`、logical group、resource type、array count 与 stage visibility 描述最小 binding contract，不保存 native slot；
- 是否允许额外 stage/binding 的固定规则。第一阶段 stage exact-match，额外未声明 binding 可保留，但所需 binding 必须精确匹配。

type 对应整个 `ShaderMapProgram`，不是 UE4.27 的单 stage `FShaderType`。采用 UE 的 `GlobalShaderType`/`GlobalShaderMap` 术语是因为职责相近，但 Toy3d 不需要为当前 Program 数据模型拆 stage 或复制 UE registration machinery。

每个具体 Pass 暴露一个返回 `const GlobalShaderType&` 的普通函数。函数局部 `static const` 只用于不可变 descriptor 生命周期，不执行注册、不访问 I/O/RHI；它不是可变全局 singleton。`GlobalShaderMap` 按 `type_name` 建索引，并在构建时拒绝重复 type name 或相同 type name 的不同 descriptor；查询同时比较 descriptor identity，不能仅以传入对象地址为 key。

`ShaderPlatform` 由 `GlobalShaderMap::load()` 的单独参数统一施加到本次所有 type 的 `ShaderMapProgramKey`。这样同一个 type descriptor 可用于 Vulkan/D3D11/D3D12，且 Pass 没有 platform 字段。

Vulkan 枚举值统一采用 `VulkanES31`，文档统一写作 `Vulkan ES3.1 profile`。该命名借鉴 UE4.27 的 `SP_VULKAN_PCES3_1` / `SP_VULKAN_ES3_1_ANDROID` feature-tier 术语，但不复制平台后缀：Toy3d 当前同一个 Shader profile 同时作为桌面 Vulkan 开发路径与移动端公共基线。它仍精确定义为 Vulkan 1.1、SPIR-V 1.3、最多四个 bound descriptor sets且不默认依赖可选device feature。

重命名同时覆盖runtime `ShaderPlatform`与离线`ShaderCompileProfile`，保留既有枚举序列化数值，保证现有ShaderMapEntry可继续读取；不保留`VulkanPortableV1` alias，避免两套正式拼写长期并存。没有选择`VulkanSM5`，因为它会暗示当前移动基线并未承诺的桌面SM5 feature tier；没有选择`Vulkan11`，因为API版本不足以表达Shader与binding能力层级。

替代方案是让 type 保存 validation callback。否决原因是开放 callback 会把任意 Pass 逻辑带入通用 map，并使 schema 难以比较和测试。显式 binding requirement 数据足以覆盖当前 Tonemap/ImGui contract。

### 4. Optional 功能通过 required type 集合的包含关系表达

不新增 `PassEligibility`、`GlobalShaderEligibility`、`GlobalShaderLoadPolicy` 或 `GlobalShaderLoadRequest`。composition root 先读取 feature configuration，再组装本次必须成功的 `const GlobalShaderType*` 集合：

- Tonemap 总是在集合中；
- ImGui disabled 时不在集合中，也不触发 I/O；
- ImGui enabled 时加入集合，并与 Tonemap 一样必须成功。

`GlobalShaderMap::load(shader_map, shader_platform, required_types)` 全有或全无地构建 candidate。所有 entries 加载并验证后，返回共享不可变 map；失败只返回诊断，不泄漏 partial map。

这比通用“eligibility”对象更准确：当前问题不是某个 Mesh 是否可进入 Pass，而是 composition root 本次是否启用一个内置 renderer feature。UE 中类似的 `ShouldCompilePermutation` 解决 Cook/compile permutation 筛选，也不应拿来命名 runtime feature selection。

### 5. `RHIShaderProgramCache` 包装现有转换链，而非复制 RHIDevice cache

cache 构造时接收一个 non-owning `RHIDevice&`，由 Renderer 在 device initialize 后创建。公开操作只有 logical RT 的 `find_or_create(const ShaderMapProgramRef&)` 与 teardown/clear；空 Program、device mismatch/terminal 和 descriptor 转换失败均在调用 backend 前返回。

现有 `RHIShaderProgram` 继续作为 RenderCore 的 stage refs + binding layout 聚合。cache entry 使用 `std::shared_ptr<const RHIShaderProgram>`，以便 cache、Tonemap/ImGui resources 和 frame-local draw preparation 共享 CPU ownership；底层 RHI refs 仍按 command list/completion contract保活。

`RHIShaderProgramKey` 按以下顺序构造并做完整 equality：

1. shader/pass identity、permutation key 与 mapping version；
2. logical layout hash、target binding hash；
3. 每个 stage 的 stage、entry point、content hash；
4. canonical binding layout entries 的 group、target binding、type、stages、array count。

unordered container 的 hash 只负责定位 bucket，命中后比较上述所有字段。这里的 collision 防护是容器 hash collision；Shader content 本身继续使用已验证 SHA-256 contract。key 不包含 debug name、Program/RHI object 地址或 vector capacity。

cache miss 调用现有 `build_rhi_shader_program_desc()`，然后经公共 `RHIDevice::create_binding_layout()` 与 `create_shader()` 建 candidate。全部 stage 成功后才插入；失败释放局部 refs且不记 negative entry。当前只允许 logical RT，因此不引入 mutex/single-flight；这项限制在接口注释和断言/错误路径中明确。

现有公开 free function `create_rhi_shader_program()` 在所有调用方迁移后删除，防止绕过 cache；descriptor builder 保留，供 cache 与转换测试复用。以后若离线工具只需 descriptor，可复用 builder 而不持有 device。

替代方案是仅让 Tonemap/ImGui 各自缓存 Program；否决原因是 Base Pass/Material 会继续复制创建与生命周期。另一个方案是直接复用 graphics pipeline cache；否决原因是 Program 生命周期和 identity 独立于 attachment/vertex/raster PSO compatibility。

### 6. Shader graphics state adapter 只负责 Shader-owned 字段

新增 `shader_graphics_state.*`，提供普通函数：以一个已由 Pass 填入 Program refs、vertex layouts、attachment formats/sample count和debug name的基础 `RHIGraphicsPipelineDesc` 加 `ShaderGraphicsPassState` 为输入，返回一个新的完整 descriptor candidate。adapter 填充：

- primitive topology；
- rasterization；
- depth/stencil；
- shader state 当前统一应用到 active color attachments 的 blend/write mask。

attachment format/count/sample、vertex input、shader refs和binding layout仍由调用方提供，因为它们是 Pass/mesh compatibility，不属于 `.shader` Pass template。viewport/scissor、stencil reference和blend constants继续是 command context 动态状态。

每个 enum conversion 返回 checked result；adapter 先写局部 candidate，所有字段成功后才返回，失败不修改调用方输入。错误诊断包含 shader/pass identity（由调用方传入 debug name）和字段。现有 `to_rhi_*` 与 `apply_shader_graphics_pass_state` 从 `forward_scene_renderer.cpp` 删除；Tonemap/ImGui 若需要相同状态也使用该 adapter。

替代方案是把转换放入 Shader format library；否决原因是中立 format target 不能依赖 runtime RHI。放入 RHI backend 同样不成立，因为源类型是跨 target Shader format，而不是 Vulkan/D3D 原生枚举。

### 7. Renderer 只接收冻结 map，并明确创建与销毁顺序

Renderer 构造签名把 `tonemap_program`/`imgui_program` 替换为一个 `std::shared_ptr<const GlobalShaderMap>`；font atlas 继续作为 enable-ImGui 的 owned bootstrap payload。Renderer 不接收 `ShaderMap` 或 loader。

logical RT bootstrap 顺序为：

1. 创建并 initialize `RHIDevice`；
2. 创建 `RHIShaderProgramCache`；
3. 创建 `RenderResourceManager` 与 RenderScene 基础对象；
4. 从 GlobalShaderMap 查询 required CPU Program，通过 cache 建 RHI Program，并建立 Tonemap/启用的 ImGui candidate resources；
5. 使用现有 device context 完成 placeholder/font upload、submit与指定 completion wait；
6. 创建 primary viewport；
7. 原子发布 Pass resources、Scene façade 与 Running。

失败逆序回滚，不发布任何 candidate。正常/terminal teardown 先清 Scene 与 resources，再释放 Pass-owned Program refs，随后销毁 cache，最后处理 placeholder、viewport、queue/device。GlobalShaderMap 只是 Renderer 的共享只读输入；RT teardown 释放自己的 ref，Engine 在 join 后释放最终 owner。

Base Pass 在每帧仍根据 material `ShaderMapProgram` 选择 Program，但改为向同一个 cache 查询。该步骤只消除 RHI Program 重建，不改变本 change 明确排除的 Material/MeshPass selection 结构。

### 8. 错误分成 CPU map failure 与 RHI bootstrap failure，但不抹平原始诊断

- GT 构建 `GlobalShaderMap` 失败：Engine 尚未启动 RenderingThread；记录失败 type、ShaderMap key与 loader/validation 诊断，按 CPU 初始化阶段回滚。
- RT cache 创建失败：沿现有 `RHIStatus` 保留 `InvalidArgument`、`Unsupported`、`OutOfMemory`、`DeviceLost` 或 `BackendFailure`，成为 Renderer first error。
- adapter failure：在 pipeline 创建前返回；若发生于 startup candidate 则 bootstrap失败，若发生于正常 frame 的当前实现则按既有 Renderer fatal frame policy进入 terminal，不能静默跳过并改变画面。
- cleanup failure：只追加 secondary diagnostic，不覆盖 first error。

不创建新的跨模块通用 error system；CPU GlobalShaderMap 延续 ShaderMap 的结构化成功值加文本诊断，RHI 路径延续 `RHIResult/RHIStatus`。

### 9. 验证按纯 CPU、fake device 与 Renderer 生命周期三层组织

- `shader_graphics_state_tests`：覆盖每个合法 enum、active color attachment 数量、reversed-Z默认/显式状态、非法枚举无默认回退，以及输入 descriptor不被失败部分修改。
- `rhi_shader_program_cache_tests`：fake device 计数验证 hit只创建一次、不同layout/stage产生miss、容器hash collision仍比较 equality、各创建步骤 fault injection不发布、重试和clear顺序。
- `global_shader_map_tests`：覆盖type name冲突、统一platform key、stage/binding mismatch、required集合全有或全无、ImGui未选中不I/O、冻结后typed lookup，以及`VulkanES31` runtime/compiler identity与既有序列化数值兼容。
- 更新 Tonemap/ImGui tests：Pass不再检查字符串/platform或直接创建RHI object；缺失type和RHI失败沿bootstrap返回。
- 更新 Forward/Renderer tests：连续两帧同一 Base Pass Program只有一次RHI创建；构造签名不含逐项Program；正常和DeviceLost teardown验证Pass/cache早于device释放且first error不被覆盖。
- 最终独立构建验证覆盖 `Toy3dEditor`、新增/受影响测试targets和CTest。Vulkan做真实draw/present冒烟；D3D11/D3D12未实现时至少编译公共路径并验证Unsupported，不把Vulkan结果宣称为三后端运行通过。

## Risks / Trade-offs

- [Risk] `GlobalShaderType` 的静态 expected binding schema 与 Shader asset 演进不同步 → 由 GlobalShaderMap 构建在启动前严格对比并报告具体parameter；测试使用实际产物覆盖Tonemap/ImGui。
- [Risk] cache key 漏字段导致错误RHI Program复用 → 从已验证Program构造独立完整key，单测覆盖逐字段差异和容器hash collision；debug name明确排除。
- [Risk] cache使用共享Program wrapper增加CPU引用层级 → 该开销只发生在Program粒度且换取统一生命周期；不在每draw分配wrapper，frame路径只复制shared ref或借用已保活ref。
- [Risk] 当前RT-only cache限制未来Pass并行 → 明确不承诺并发；未来并行change必须选择bootstrap冻结或内部同步并增加single-flight，现接口无需暴露backend细节。
- [Risk] 新adapter仍接受半成品pipeline desc，调用者可能遗漏attachment/vertex字段 → 返回后继续经过现有`RHIDevice::create_graphics_pipeline()` NVI完整validation；adapter测试只保证Shader-owned字段。
- [Risk] Engine在RenderingThread之前同步加载Global Shaders增加启动时延 → 当前本就同步加载Tonemap/ImGui；本change不增加Program集合，异步/Cook library另立change。
- [Risk] `VulkanES31` 容易被误读为使用GLSL ES字节码 → 文档和枚举注释明确它是UE风格feature tier，实际产物仍是SPIR-V 1.3，RHI backend仍为Vulkan 1.1。
- [Trade-off] 具体GlobalShaderType函数留在Pass模块，使composition helper依赖这些feature headers → 依赖方向仍是RenderScene→RenderCore，且显式可搜索；换来type schema与消费代码同域维护。

## Migration Plan

1. **Adapter批次**：新增`shader_graphics_state.*`及穷举/失败测试；ForwardSceneRenderer改用adapter并在同一批删除全部本地`to_rhi_*`。不改Pass选择。
2. **Program cache批次**：在现有`rhi_shader_program.*`上新增key/cache和fake-device测试；Renderer在device后创建cache；Tonemap/ImGui/Base Pass改为cache查询；同批删除公开`create_rhi_shader_program()`直通入口和逐帧创建路径。
3. **Global Shader CPU批次**：先将runtime `ShaderPlatform`、离线`ShaderCompileProfile`及Active文档全链路重命名为`VulkanES31`/`Vulkan ES3.1 profile`，保持序列化数值且不留alias；随后新增type/map，Tonemap/ImGui在各自模块声明type，增加`renderer_builtin_shaders.*`显式集合与map测试。Engine集中选择ShaderPlatform并完成candidate加载冻结。
4. **Renderer输入与bootstrap批次**：一次性替换Renderer构造参数，Pass按type查询；更新全有或全无bootstrap、first-error和single/multi-thread tests。同批删除Engine的`tonemap_shader_program`/`imgui_shader_program`字段、硬编码keys及Pass重复identity/platform检查，不保留兼容重载。
5. **销毁与集成验证批次**：落实正常/DeviceLost逆序销毁和fault injection；构建全部受影响目标、运行CTest，再做Vulkan真实Editor冒烟并记录未覆盖后端。

每一批都保持一个正式入口；不以长期feature flag维持新旧路径。若某批验证失败，回退该批完整提交，上一批仍应可构建运行；不得只恢复Pass直调入口而保留半套cache/map。此 change 不涉及持久化格式和资产迁移，因此没有数据rollback。

## Open Questions

无。D3D11/D3D12 backend运行验证、异步Global Shader加载与MaterialShaderMap都需要独立change，不影响本设计的接口和任务拆分。
