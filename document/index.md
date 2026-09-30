# Toy3d 设计文档索引

本文是 `document/` 的规范入口。状态含义如下：

- `Active`：当前规范，涉及对应模块的设计、实现或审查应读取；
- `Draft`：尚未定型，只能作为讨论输入，不得覆盖 Active contract；
- `Historical`：历史记录，不构成当前约束；
- `Superseded`：已被其他文档替代，不构成当前约束。

AI 不应默认读取全部设计文档，只读取当前任务直接涉及的 Active 文档及其明确引用。

## 快速入口

| 领域 | 状态 | 规范入口 | 说明 |
| --- | --- | --- | --- |
| 公共模块使用 | Active | `core-module-usage-index.md` | 查找 `engine/core/` target、公共头文件和最小用例的首选入口 |
| 共享文件系统 | Active | `core-infrastructure-design.md` | 文件、路径、mount、存储后端与 I/O contract |
| 外部进程 | Active | `core-process-design.md` | compiler 和 Editor 共享的有界执行、取消及外部编辑器启动 |
| 材质源码迭代 | Active | `material-source-workflow-design.md` | 项目源码登记、外部编辑、异步编译与候选接管 |
| 编辑器资源基础 | Active | `editor-resource-foundation-design.md` | 反射、值编解码、Asset 文件、身份索引与无界面编辑 contract；行为细节见 OpenSpec change `establish-editor-resource-foundation` |
| Editor 总体架构 | Draft | `editor-architecture-design.md` | Editor 所有权、面板、选择、视口、编辑命令、场景生命周期与分批实施 |
| 编辑器资源接入方案 | Draft | `editor-resource-integration-plan.md` | 创作目录、模型生产链、资源编辑与后续资源类型的分批接入计划 |
| 代码材质与参数化编辑 | Active | `material-system-design.md` | 属性、资产、多层实例继承、共享发布、源码迭代和 Texture2D/Sampler 采样；材质预览与缩略图按 M6 实施 |
| StaticMesh 生产链 | Active | `static-mesh-import-design.md` | MeshDescription、Assimp 导入、网格构建、Asset 保存与运行时适配 |
| Asset 缩略图 | Active | `asset-thumbnail-design.md` | StaticMesh 独立预览、多 UI 纹理、异步颜色读回、包内 PNG 与缩略图池 contract |
| Core Math | Active | `core-math-design.md` | 数学类型、坐标、矩阵、Transform 与迁移 contract |
| 线程与 Task Graph | Active | `threading-task-graph-design.md` | 共享线程、Queue、GraphTask、Named Thread 与同步 contract |
| GameScene | Active | `gamescene-design.md` | World、Actor、Component、注册与 GameScene 生命周期 |
| Game/Render 多线程与 Renderer Foundation | Active | `../openspec/specs/game-render-framework/` | 1 个总控与 17 个子 capability specs 是当前框架行为 contract 的唯一规范入口；已完成 change 的设计与任务记录位于 OpenSpec archive |
| RHI | Active | `rhi-design.md` | 公共 RHI、资源、命令、同步、typed parameter/RHI 分层、后端与上层边界 |
| RHI Binding 聚合 | Active | `rhi-binding-aggregation-design.md` | 五个逻辑 Binding Group、RenderCore typed 创建边界与跨后端聚合 contract |
| Shader 系统 | Active | `shader-system-design.md` | Shader 语言、编译、generated C++ parameters、反射、ShaderMap、Binding ABI 与运行时加载 |
| Shader 语言语法 | Active | `shader-language-v1.ebnf` | `.shader` v1 语法定义 |
| Vulkan 内存 | Active | `vulkan-memory-management.md` | Vulkan/VMA、上传、completion 与延迟销毁；旧实现评估仅是历史背景 |
| Runtime Console | Active | `console-manager-design.md` | runtime 配置变量、覆盖顺序与生命周期 |
| 资源目录与部署 | Active | `resource-directory-design.md` | 引擎/项目资产、配置、Editor 界面资源与平台构建输入的职责及部署 |
| Application 与验证项目 | Active | `application-design.md` | 项目侧启动策略、Engine 绑定边界与当前 Cube 案例组织 |

## 历史文档


历史文档统一位于 `archive/`，默认不得作为 AI 上下文或实现依据：

| 文档 | 状态 | 替代入口或归档原因 |
| --- | --- | --- |
| `archive/legacy-readme.txt` | Superseded | 项目结构以 `AGENTS.md` 为准 |
| `archive/rendering-engine-foundation-progress.md` | Historical | 旧 Foundation 施工与验证台账；当前状态以代码、测试和专项设计为准 |
| `archive/game-render-thread-design.md` | Superseded | 旧版 Game/Render 线程方案，最终由 OpenSpec change `establish-game-render-framework` 取代 |
| `archive/game-render-thread-framework-design.md` | Superseded | 框架讨论中间稿，包含已撤销的命令参数设计；由 OpenSpec change `establish-game-render-framework` 取代 |
| `archive/rendering-engine-foundation-design.md` | Superseded | 旧 Renderer Foundation 总设计，包含已撤销的资源缓存与跨线程身份设计；由 OpenSpec change `establish-game-render-framework` 取代 |

## 维护规则

- 新增设计文档时必须在本索引登记状态、领域和规范入口。
- 新文档取代旧文档时，同一批次更新所有规范性引用，并将旧文档标记为 `Superseded` 或移入 `archive/`。
- Active 文档不得同时宣称两个相互冲突的“唯一入口”。
- 仅记录构建命令、提交号和逐轮验证结果的内容应进入提交记录、PR 或历史台账，不应长期占用 Active 设计文档。
